#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "check.h"
#include "draz/store.h"

using namespace draz;
namespace fs = std::filesystem;

namespace {

// A store in its own empty folder, with fast hashing and a clock the test
// controls.
struct Fixture {
    fs::path dir;
    std::int64_t time = 1772452800;
    std::unique_ptr<Store> store;

    explicit Fixture(const std::string& name) : dir(fs::temp_directory_path() / ("draz_test_" + name)) {
        fs::remove_all(dir);
        reopen();
    }
    ~Fixture() { fs::remove_all(dir); }

    // Loads everything again from disk, like restarting the program.
    Store& reopen() {
        Store::Options options;
        options.dataDir = dir;
        options.iterations = 1000;
        options.clock = [this] { return time; };
        store = std::make_unique<Store>(options);
        return *store;
    }

    const Account& signIn(Role role, const std::string& name, const std::string& password = "shop-pass-1") {
        LoginResult r = store->login(role, name, password);
        if (r.status != LoginStatus::Ok) throw std::runtime_error("login failed for " + name);
        return *r.account;
    }

    std::string file(const std::string& name) const {
        std::ifstream in(dir / name);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
};

// An admin, two customers and three products.
struct Shop : Fixture {
    explicit Shop(const std::string& name) : Fixture(name) {
        const Account& admin = store->createFirstAdmin("admin", "shop-pass-1");
        store->registerCustomer("ali", "shop-pass-1");
        store->registerCustomer("sara", "shop-pass-1");
        store->addProduct(admin, 101, "Wireless Earbuds", 2500, 10);
        store->addProduct(admin, 102, "USB-C Cable", 400, 50);
        store->addProduct(admin, 103, "Smart Watch", 7000, 2);
    }
    const Account& admin() { return signIn(Role::Admin, "admin"); }
    const Account& ali() { return signIn(Role::Customer, "ali"); }
    const Account& sara() { return signIn(Role::Customer, "sara"); }
};

}  // namespace

// ---------------------------------------------------------------- accounts

TEST("first run creates the admin, and admin/1234 is gone") {
    Fixture f("setup");
    CHECK(f.store->needsSetup());
    f.store->createFirstAdmin("owner", "shop-pass-1");
    CHECK(!f.store->needsSetup());
    CHECK_THROWS(StoreError, f.store->createFirstAdmin("second", "shop-pass-1"));
    CHECK(f.store->login(Role::Admin, "admin", "1234").status == LoginStatus::WrongCredentials);
}

TEST("old bug: customer passwords were stored in plain text") {
    Fixture f("plaintext");
    f.store->registerCustomer("ali", "secret-pass-99");
    CHECK(f.file("users.tsv").find("secret-pass-99") == std::string::npos);
    CHECK(f.file("users.tsv").find("pbkdf2-sha256$1000$") != std::string::npos);
}

TEST("usernames are unique and passwords follow the rules") {
    Shop f("accounts");
    CHECK_THROWS(StoreError, f.store->registerCustomer("ALI", "another-pass"));
    CHECK_THROWS(StoreError, f.store->registerCustomer("admin", "another-pass"));
    CHECK_THROWS(StoreError, f.store->registerCustomer("bo", "another-pass"));
    CHECK_THROWS(StoreError, f.store->registerCustomer("bilal", "short"));
    CHECK(f.store->login(Role::Customer, "Ali", "shop-pass-1").status == LoginStatus::Ok);
    CHECK(f.store->login(Role::Admin, "ali", "shop-pass-1").status == LoginStatus::WrongCredentials);
}

TEST("five wrong passwords lock the account, even across a restart") {
    Shop f("lockout");
    for (int i = 0; i < 4; ++i) f.store->login(Role::Customer, "ali", "wrong");
    CHECK(f.store->login(Role::Customer, "ali", "wrong").status == LoginStatus::Locked);
    f.reopen();
    CHECK(f.store->login(Role::Customer, "ali", "shop-pass-1").status == LoginStatus::Locked);
    f.time += 301;
    CHECK(f.store->login(Role::Customer, "ali", "shop-pass-1").status == LoginStatus::Ok);
}

TEST("only the admin can reset a customer password") {
    Shop f("reset");
    CHECK_THROWS(StoreError, f.store->resetPassword(f.sara(), "ali", "taken-over-1"));
    f.store->resetPassword(f.admin(), "ali", "new-pass-123");
    CHECK(f.store->login(Role::Customer, "ali", "new-pass-123").status == LoginStatus::Ok);
}

// ---------------------------------------------------------------- products

TEST("old bug: product names with spaces broke the data file") {
    Shop f("spaces");
    f.reopen();
    std::optional<Product> p = f.store->findProduct(101);
    CHECK(p.has_value());
    CHECK_EQ(p->name, std::string("Wireless Earbuds"));
    CHECK_EQ(p->price, Money(2500));
}

TEST("old bug: changing a product ID made it impossible to buy") {
    Shop f("change_id");
    f.store->updateProduct(f.admin(), 102, 199, "USB-C Cable 2m", 450, 40);
    CHECK(!f.store->findProduct(102).has_value());
    CHECK(f.store->findProduct(199).has_value());
    f.store->addToCart(f.ali(), 199, 2);
    CHECK_EQ(f.store->checkout(f.ali()).bill.subtotal, Money(900));
}

TEST("product rules: unique IDs, ranges, admin only") {
    Shop f("product_rules");
    const Account& admin = f.admin();
    CHECK_THROWS(StoreError, f.store->addProduct(admin, 101, "Copy", 100, 1));
    CHECK_THROWS(StoreError, f.store->addProduct(admin, 0, "Zero", 100, 1));
    CHECK_THROWS(StoreError, f.store->addProduct(admin, 200, "Free", 0, 1));
    CHECK_THROWS(StoreError, f.store->addProduct(admin, 200, "Negative", 100, -1));
    CHECK_THROWS(StoreError, f.store->addProduct(admin, 200, "x", 100, 1));
    CHECK_THROWS(StoreError, f.store->addProduct(f.ali(), 200, "Sneaky", 100, 1));
    CHECK_THROWS(StoreError, f.store->updateProduct(admin, 101, 102, "Clash", 100, 1));
    CHECK_EQ(f.store->products().size(), std::size_t(3));
}

TEST("deleting a product also removes it from every cart") {
    Shop f("delete");
    f.store->addToCart(f.ali(), 103, 1);
    f.store->removeProduct(f.admin(), 103);
    CHECK(f.store->cart(f.ali()).empty());
    CHECK_THROWS(StoreError, f.store->removeProduct(f.admin(), 103));
}

TEST("search and sort") {
    Shop f("search");
    CHECK_EQ(f.store->searchProducts("watch").size(), std::size_t(1));
    CHECK_EQ(f.store->searchProducts("USB").size(), std::size_t(1));
    CHECK(f.store->searchProducts("").empty());
    std::vector<Product> cheap = f.store->productsByPrice(true);
    CHECK_EQ(cheap.front().id, 102);
    CHECK_EQ(cheap.back().id, 103);
    CHECK_EQ(f.store->productsByPrice(false).front().id, 103);
}

TEST("products are kept in a balanced tree after loading") {
    Fixture f("balanced");
    const Account& admin = f.store->createFirstAdmin("admin", "shop-pass-1");
    for (int id = 1; id <= 100; ++id) f.store->addProduct(admin, id, "Item " + std::to_string(id), 10, 1);
    CHECK_EQ(f.store->stats().treeHeight, 100);  // added in ID order: a chain
    f.reopen();
    CHECK_EQ(f.store->stats().treeHeight, 7);
    CHECK_EQ(f.store->stats().products, std::size_t(100));
}

// ---------------------------------------------------------------- cart and checkout

TEST("old bug: every customer shared one cart") {
    Shop f("own_cart");
    f.store->addToCart(f.ali(), 101, 1);
    CHECK(f.store->cart(f.sara()).empty());
    CHECK_EQ(f.store->cart(f.ali()).size(), std::size_t(1));
}

TEST("old bug: adding to the cart took stock even without buying") {
    Shop f("stock_held");
    f.store->addToCart(f.ali(), 101, 3);
    CHECK_EQ(f.store->findProduct(101)->stock, 10);
    f.reopen();
    CHECK_EQ(f.store->findProduct(101)->stock, 10);
    CHECK_EQ(f.store->cart(f.ali()).front().quantity, 3);  // the cart survives a restart
    f.store->checkout(f.ali());
    CHECK_EQ(f.store->findProduct(101)->stock, 7);
}

TEST("the cart is a stack: undo removes the last added product") {
    Shop f("cart_stack");
    f.store->addToCart(f.ali(), 101, 1);
    f.store->addToCart(f.ali(), 102, 2);
    std::optional<CartItem> undone = f.store->removeLastAdded(f.ali());
    CHECK(undone.has_value());
    CHECK_EQ(undone->productId, 102);
    CHECK_EQ(f.store->cart(f.ali()).size(), std::size_t(1));
    f.store->removeFromCart(f.ali(), 101);
    CHECK(f.store->cart(f.ali()).empty());
    CHECK(!f.store->removeLastAdded(f.ali()).has_value());
}

TEST("adding the same product again adds to its quantity, up to the stock") {
    Shop f("cart_qty");
    f.store->addToCart(f.ali(), 103, 1);
    f.store->addToCart(f.ali(), 103, 1);
    CHECK_EQ(f.store->cart(f.ali()).size(), std::size_t(1));
    CHECK_EQ(f.store->cart(f.ali()).front().quantity, 2);
    CHECK_THROWS(StoreError, f.store->addToCart(f.ali(), 103, 1));  // only 2 in stock
    CHECK_THROWS(StoreError, f.store->addToCart(f.ali(), 999, 1));
    CHECK_THROWS(StoreError, f.store->addToCart(f.ali(), 101, 0));
}

TEST("checkout checks stock again, places the order in full or not at all") {
    Shop f("checkout_stock");
    f.store->addToCart(f.ali(), 103, 2);
    f.store->addToCart(f.sara(), 103, 1);
    f.store->addToCart(f.sara(), 102, 5);
    f.store->checkout(f.ali());  // takes both watches
    CHECK_THROWS(StoreError, f.store->checkout(f.sara()));
    CHECK_EQ(f.store->findProduct(102)->stock, 50);  // nothing taken from the failed order
    CHECK_EQ(f.store->cart(f.sara()).size(), std::size_t(2));
    CHECK_THROWS(StoreError, f.store->checkout(f.ali()));  // empty cart
}

TEST("discount tiers") {
    CHECK_EQ(discountPercentFor(999), 0);
    CHECK_EQ(discountPercentFor(1000), 5);
    CHECK_EQ(discountPercentFor(4999), 5);
    CHECK_EQ(discountPercentFor(5000), 7);
    CHECK_EQ(discountPercentFor(10000), 12);
    CHECK_EQ(discountPercentFor(20000), 20);
    Bill b = makeBill(12345);
    CHECK_EQ(b.discount, Money(1481));
    CHECK_EQ(b.total, Money(10864));
}

// ---------------------------------------------------------------- orders

TEST("orders wait in a queue and are processed oldest first") {
    Shop f("queue");
    f.store->addToCart(f.ali(), 101, 1);
    Order first = f.store->checkout(f.ali());
    f.store->addToCart(f.sara(), 102, 1);
    Order second = f.store->checkout(f.sara());
    CHECK_EQ(f.store->pendingOrders().size(), std::size_t(2));

    f.reopen();  // the queue is rebuilt from the saved orders
    CHECK_EQ(f.store->pendingOrders().front().id, first.id);
    CHECK_EQ(f.store->processNextOrder(f.admin()).id, first.id);
    CHECK_EQ(f.store->processNextOrder(f.admin()).id, second.id);
    CHECK_THROWS(StoreError, f.store->processNextOrder(f.admin()));
    CHECK_THROWS(StoreError, f.store->processNextOrder(f.ali()));
}

TEST("old bug: buying history only showed the current cart") {
    Shop f("history");
    f.store->addToCart(f.ali(), 101, 1);
    f.store->checkout(f.ali());
    f.store->addToCart(f.ali(), 102, 3);
    f.store->checkout(f.ali());
    f.reopen();
    std::vector<Order> orders = f.store->ordersOf(f.ali());
    CHECK_EQ(orders.size(), std::size_t(2));
    CHECK_EQ(orders[0].lines[0].productId, 102);  // newest first
    CHECK_EQ(orders[1].bill.total, Money(2375));  // 2500 minus 5%
    CHECK(f.store->ordersOf(f.sara()).empty());
}

TEST("returns: only delivered orders, not more than bought, refund minus discount") {
    Shop f("returns");
    f.store->addToCart(f.ali(), 101, 2);
    Order order = f.store->checkout(f.ali());  // 5000, 7% off
    CHECK_THROWS(StoreError, f.store->returnItem(f.ali(), order.id, 101, 1));  // not delivered yet
    f.store->processNextOrder(f.admin());

    CHECK_THROWS(StoreError, f.store->returnItem(f.sara(), order.id, 101, 1));  // not her order
    CHECK_THROWS(StoreError, f.store->returnItem(f.ali(), order.id, 102, 1));   // not in the order
    CHECK_THROWS(StoreError, f.store->returnItem(f.ali(), order.id, 101, 3));   // bought only 2
    CHECK_EQ(f.store->returnItem(f.ali(), order.id, 101, 1), Money(2325));
    CHECK_EQ(f.store->findProduct(101)->stock, 9);
    CHECK_EQ(f.store->returnItem(f.ali(), order.id, 101, 1), Money(2325));
    CHECK_THROWS(StoreError, f.store->returnItem(f.ali(), order.id, 101, 1));
    f.reopen();
    CHECK_EQ(f.store->ordersOf(f.ali())[0].lines[0].returned, 2);
}

// ---------------------------------------------------------------- complaints

TEST("old bug: viewing complaints deleted them, and they were never saved") {
    Shop f("complaints");
    f.store->fileComplaint(f.ali(), "My earbuds arrived late");
    f.time += 60;
    f.store->fileComplaint(f.sara(), "Cable stopped working");
    CHECK_EQ(f.store->complaints().size(), std::size_t(2));
    CHECK_EQ(f.store->complaints().size(), std::size_t(2));
    f.reopen();
    std::vector<Complaint> list = f.store->complaints();
    CHECK_EQ(list.size(), std::size_t(2));
    CHECK_EQ(list[0].customer, std::string("sara"));  // newest first
    CHECK_EQ(f.store->complaintsOf(f.ali()).size(), std::size_t(1));
}

TEST("complaints: admin resolves them, rules on length") {
    Shop f("resolve");
    f.store->fileComplaint(f.ali(), "Wrong colour delivered");
    CHECK_THROWS(StoreError, f.store->fileComplaint(f.ali(), "bad"));
    CHECK_THROWS(StoreError, f.store->fileComplaint(f.admin(), "Admins can't complain"));
    int id = f.store->complaints()[0].id;
    CHECK_THROWS(StoreError, f.store->resolveComplaint(f.ali(), id));
    f.store->resolveComplaint(f.admin(), id);
    CHECK(f.store->complaints()[0].resolved);
    CHECK_THROWS(StoreError, f.store->resolveComplaint(f.admin(), id));
    CHECK_EQ(f.store->stats().openComplaints, std::size_t(0));
}

TEST("broken lines in data files are skipped, not trusted") {
    Fixture f("corrupt");
    fs::create_directories(f.dir);
    {
        std::ofstream users(f.dir / "users.tsv");
        users << "admin\tmallory\tplaintext-password\t0\t0\n";
        users << "root\tx\tpbkdf2-sha256$1$00$00\t0\t0\n";
    }
    {
        std::ofstream products(f.dir / "products.tsv");
        products << "101\tGood\t100\t5\n";
        products << "abc\tBad ID\t100\t5\n";
        products << "102\tBad price\tfree\t5\n";
    }
    f.reopen();
    CHECK(f.store->needsSetup());
    CHECK_EQ(f.store->products().size(), std::size_t(1));
}
