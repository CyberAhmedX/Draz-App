#include "draz/app.h"

#include <ctime>
#include <iostream>

namespace draz {

namespace c = console;

namespace {

std::string when(std::int64_t unixTime) {
    std::time_t t = static_cast<std::time_t>(unixTime);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%d %b %Y %H:%M", &local);
    return buffer;
}

std::string plural(long long n, const std::string& word) {
    return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
}

std::string passwordRules() {
    return "At least " + std::to_string(Store::kMinPassword) + " characters, not the same as the username.";
}

// Prints the subtotal, discount and total, right-aligned to `width` columns
// so they line up under the table above them.
void billLines(const Bill& bill, std::size_t width = 46) {
    auto line = [&](const std::string& label, const std::string& value) {
        std::cout << "  " << label << std::string(width - label.size() - value.size(), ' ') << value << "\n";
    };
    std::cout << "\n";
    line("Subtotal", c::money(bill.subtotal));
    line("Discount (" + std::to_string(bill.discountPercent) + "%)", "-" + c::money(bill.discount));
    std::string rule;
    for (std::size_t i = 0; i < width; ++i) rule += "─";
    std::cout << "  " << rule << "\n";
    line("Total", c::money(bill.total));
}

}  // namespace

// ---------------------------------------------------------------- screen

void Screen::run() {
    while (true) {
        c::clear();
        c::header(path(), status());
        int choice = c::menu(items());
        bool stay = true;
        skipPause_ = false;
        try {
            stay = handle(choice);
        } catch (const StoreError& e) {
            c::fail(e.what());
            skipPause_ = false;
        }
        if (!stay) return;
        if (!skipPause_) c::pause();
    }
}

void Screen::top(const std::string& page) const {
    c::clear();
    std::vector<std::string> p = path();
    p.push_back(page);
    c::header(p, status());
}

void Screen::changePassword() {
    top("Password");
    c::section("Change your password");
    std::string current = c::readPassword("Current password: ");
    c::hint(passwordRules());
    std::string next = c::readPassword("New password: ");
    if (c::readPassword("Repeat new password: ") != next) throw StoreError("The new passwords don't match.");
    store_.changePassword(account_, current, next);
    c::ok("Password changed.");
}

void Screen::showProducts(const std::vector<Product>& products) const {
    if (products.empty()) {
        c::note("No products to show.");
        return;
    }
    std::vector<std::vector<std::string>> rows;
    for (const Product& p : products) {
        rows.push_back({std::to_string(p.id), p.name, c::money(p.price),
                        p.stock == 0 ? "sold out" : std::to_string(p.stock)});
    }
    c::table({{"ID", 6, true}, {"Product", 30}, {"Price", 12, true}, {"Stock", 8, true}}, rows);
}

void Screen::showReceipt(const Order& order) {
    std::cout << "  Order #" << order.id << "  ·  " << when(order.placedAt) << "  ·  "
              << (order.status == OrderStatus::Pending ? "waiting in queue" : "delivered") << "\n\n";
    std::vector<std::vector<std::string>> rows;
    for (const OrderLine& l : order.lines) {
        std::string qty = std::to_string(l.quantity);
        if (l.returned) qty += " (" + std::to_string(l.returned) + " returned)";
        rows.push_back({std::to_string(l.productId), l.name, qty, c::money(l.price * l.quantity)});
    }
    c::table({{"ID", 6, true}, {"Product", 22}, {"Qty", 14}, {"Amount", 12, true}}, rows);
    billLines(order.bill, 60);
}

// ---------------------------------------------------------------- customer

std::string CustomerScreen::status() const {
    std::size_t count = 0;
    for (const CartItem& item : store_.cart(account_)) count += static_cast<std::size_t>(item.quantity);
    return account_.username() + "  ·  cart " + std::to_string(count);
}

std::vector<c::MenuItem> CustomerScreen::items() const {
    std::vector<CartItem> cart = store_.cart(account_);
    std::string cartDetail = cart.empty() ? "empty" : plural(static_cast<long long>(cart.size()), "product") + ", " +
                                                          c::money(store_.cartBill(account_).total);
    std::size_t orders = store_.ordersOf(account_).size();
    return {{1, "Browse products", "sorted by ID"},
            {2, "Sort by price", ""},
            {3, "Search by name", ""},
            {4, "Add to cart", ""},
            {5, "My cart", cartDetail},
            {6, "Checkout", ""},
            {7, "My orders", orders ? plural(static_cast<long long>(orders), "order") : ""},
            {8, "Return an item", ""},
            {9, "Complaints", ""},
            {10, "Change password", ""},
            {0, "Log out", ""}};
}

bool CustomerScreen::handle(int choice) {
    switch (choice) {
        case 1: browse(); break;
        case 2: sortByPrice(); break;
        case 3: search(); break;
        case 4: addToCart(); break;
        case 5: viewCart(); break;
        case 6: checkout(); break;
        case 7: myOrders(); break;
        case 8: returnItem(); break;
        case 9: complaints(); break;
        case 10: changePassword(); break;
        default: return false;
    }
    return true;
}

void CustomerScreen::browse() {
    top("Products");
    c::section("All products");
    showProducts(store_.products());
}

void CustomerScreen::sortByPrice() {
    top("Products");
    c::section("Sort by price");
    int order = c::menu({{1, "Lowest first", ""}, {2, "Highest first", ""}, {0, "Back", ""}});
    if (order == 0) return back();
    std::cout << "\n";
    showProducts(store_.productsByPrice(order == 1));
}

void CustomerScreen::search() {
    top("Search");
    c::section("Search by name");
    std::string text = c::readLine("Search for: ");
    std::cout << "\n";
    showProducts(store_.searchProducts(text));
}

void CustomerScreen::addToCart() {
    top("Add to cart");
    c::section("Add to cart");
    showProducts(store_.products());
    std::cout << "\n";
    int id = c::readInt("Product ID (0 to cancel): ", 0, Store::kMaxProductId);
    if (id == 0) return back();
    std::optional<Product> p = store_.findProduct(id);
    if (!p) throw StoreError("No product with ID " + std::to_string(id) + ".");
    int qty = c::readInt("Quantity: ", 1, Store::kMaxPerLine);
    store_.addToCart(account_, id, qty);
    c::ok("Added " + std::to_string(qty) + " x " + p->name + " to your cart.");
}

void CustomerScreen::viewCart() {
    while (true) {
        top("Cart");
        c::section("My cart");
        std::vector<CartItem> cart = store_.cart(account_);
        if (cart.empty()) {
            c::note("Your cart is empty.");
            return;
        }
        std::vector<std::vector<std::string>> rows;
        for (const CartItem& item : cart) {
            rows.push_back({std::to_string(item.productId), item.name, std::to_string(item.quantity),
                            c::money(item.price), c::money(item.price * item.quantity)});
        }
        c::table({{"ID", 6, true}, {"Product", 24}, {"Qty", 4, true}, {"Price", 10, true}, {"Amount", 12, true}}, rows);
        billLines(store_.cartBill(account_), 64);
        std::cout << "\n";

        int choice = c::menu({{1, "Remove last added", ""}, {2, "Remove a product", ""}, {0, "Back", ""}});
        if (choice == 0) return back();
        try {
            if (choice == 1) {
                std::optional<CartItem> item = store_.removeLastAdded(account_);
                if (item) c::ok("Removed " + item->name + ".");
            } else {
                int id = c::readInt("Product ID to remove: ", 1, Store::kMaxProductId);
                store_.removeFromCart(account_, id);
                c::ok("Removed.");
            }
        } catch (const StoreError& e) {
            c::fail(e.what());
        }
        c::pause();
    }
}

void CustomerScreen::checkout() {
    top("Checkout");
    c::section("Checkout");
    std::vector<CartItem> cart = store_.cart(account_);
    if (cart.empty()) throw StoreError("Your cart is empty.");
    for (const CartItem& item : cart) {
        std::cout << "  " << item.quantity << " x " << item.name << "\n";
    }
    billLines(store_.cartBill(account_));
    std::cout << "\n";
    if (!c::confirm("Place this order?")) return back();
    Order order = store_.checkout(account_);
    c::ok("Order #" + std::to_string(order.id) + " placed. It is number " +
          std::to_string(store_.pendingOrders().size()) + " in the delivery queue.");
}

void CustomerScreen::myOrders() {
    top("Orders");
    c::section("My orders");
    std::vector<Order> orders = store_.ordersOf(account_);
    if (orders.empty()) {
        c::note("You haven't placed any orders yet.");
        return;
    }
    std::vector<std::vector<std::string>> rows;
    for (const Order& o : orders) {
        int units = 0;
        for (const OrderLine& l : o.lines) units += l.quantity;
        rows.push_back({"#" + std::to_string(o.id), when(o.placedAt), std::to_string(units),
                        c::money(o.bill.total), o.status == OrderStatus::Pending ? "in queue" : "delivered"});
    }
    c::table({{"Order", 7}, {"Placed", 18}, {"Items", 5, true}, {"Total", 12, true}, {"Status", 10}}, rows);
    std::cout << "\n";
    int id = c::readInt("Order number to open (0 to go back): ", 0, 999999999);
    if (id == 0) return back();
    for (const Order& o : orders) {
        if (o.id != id) continue;
        std::cout << "\n";
        showReceipt(o);
        return;
    }
    throw StoreError("You have no order #" + std::to_string(id) + ".");
}

void CustomerScreen::returnItem() {
    top("Returns");
    c::section("Return an item");
    std::vector<std::vector<std::string>> rows;
    for (const Order& o : store_.ordersOf(account_)) {
        if (o.status != OrderStatus::Delivered) continue;
        for (const OrderLine& l : o.lines) {
            int left = l.quantity - l.returned;
            if (left > 0) rows.push_back({"#" + std::to_string(o.id), std::to_string(l.productId), l.name, std::to_string(left)});
        }
    }
    if (rows.empty()) {
        c::note("Nothing to return. Only delivered orders can be returned.");
        return;
    }
    c::table({{"Order", 7}, {"ID", 6, true}, {"Product", 26}, {"Can return", 10, true}}, rows);
    std::cout << "\n";
    int orderId = c::readInt("Order number (0 to cancel): ", 0, 999999999);
    if (orderId == 0) return back();
    int productId = c::readInt("Product ID: ", 1, Store::kMaxProductId);
    int qty = c::readInt("How many: ", 1, Store::kMaxPerLine);
    Money refund = store_.returnItem(account_, orderId, productId, qty);
    c::ok("Return accepted. " + c::money(refund) + " will be refunded.");
}

void CustomerScreen::complaints() {
    top("Complaints");
    c::section("Complaints");
    int choice = c::menu({{1, "Write a complaint", ""}, {2, "My complaints", ""}, {0, "Back", ""}});
    if (choice == 1) {
        c::hint("Up to " + std::to_string(Store::kMaxComplaint) + " characters, on one line.");
        std::string text = c::readLine("Complaint: ");
        store_.fileComplaint(account_, text);
        c::ok("Complaint sent. The admin will look into it.");
    } else if (choice == 2) {
        std::vector<Complaint> list = store_.complaintsOf(account_);
        std::cout << "\n";
        if (list.empty()) c::note("You haven't written any complaints.");
        for (const Complaint& cpl : list) {
            std::cout << "  #" << cpl.id << "  " << when(cpl.createdAt) << "  "
                      << (cpl.resolved ? "[resolved]" : "[open]") << "\n     " << cpl.text << "\n\n";
        }
    } else {
        back();
    }
}

// ---------------------------------------------------------------- admin

std::string AdminScreen::status() const {
    return account_.username() + "  ·  " + plural(static_cast<long long>(store_.pendingOrders().size()), "order") + " waiting";
}

std::vector<c::MenuItem> AdminScreen::items() const {
    StoreStats s = store_.stats();
    return {{1, "All products", plural(static_cast<long long>(s.products), "product")},
            {2, "Add a product", ""},
            {3, "Edit a product", ""},
            {4, "Delete a product", ""},
            {5, "Search products", ""},
            {6, "Order queue", s.pendingOrders ? std::to_string(s.pendingOrders) + " waiting" : "empty"},
            {7, "Process next order", ""},
            {8, "Complaints", s.openComplaints ? std::to_string(s.openComplaints) + " open" : ""},
            {9, "Customers", std::to_string(s.customers)},
            {10, "Store stats", ""},
            {11, "Reset a customer password", ""},
            {12, "Change my password", ""},
            {0, "Log out", ""}};
}

bool AdminScreen::handle(int choice) {
    switch (choice) {
        case 1: allProducts(); break;
        case 2: addProduct(); break;
        case 3: editProduct(); break;
        case 4: deleteProduct(); break;
        case 5: searchProducts(); break;
        case 6: orderQueue(); break;
        case 7: processNext(); break;
        case 8: complaints(); break;
        case 9: customers(); break;
        case 10: stats(); break;
        case 11: resetPassword(); break;
        case 12: changePassword(); break;
        default: return false;
    }
    return true;
}

void AdminScreen::allProducts() {
    top("Products");
    c::section("All products");
    showProducts(store_.products());
}

void AdminScreen::addProduct() {
    top("Products");
    c::section("Add a product");
    int id = c::readInt("Product ID: ", 1, Store::kMaxProductId);
    if (std::optional<Product> taken = store_.findProduct(id)) {
        throw StoreError("Product ID " + std::to_string(id) + " is already used by " + taken->name + ".");
    }
    std::string name = c::readLine("Name: ");
    Store::cleanText(name, 2, 40, "Product name");
    int price = c::readInt("Price in Rs: ", 1, static_cast<int>(Store::kMaxPrice));
    int stock = c::readInt("Stock: ", 0, Store::kMaxStock);
    store_.addProduct(account_, id, name, price, stock);
    c::ok("Added " + Store::cleanText(name, 2, 40, "Product name") + ".");
}

void AdminScreen::editProduct() {
    top("Products");
    c::section("Edit a product");
    int id = c::readInt("Product ID to edit: ", 1, Store::kMaxProductId);
    std::optional<Product> p = store_.findProduct(id);
    if (!p) throw StoreError("No product with ID " + std::to_string(id) + ".");
    c::hint("Press Enter to keep the value in brackets.");
    std::cout << "\n";

    auto number = [](const std::string& label, int current, int min, int max) {
        while (true) {
            std::string text = c::readLine(label + " [" + std::to_string(current) + "]: ");
            if (text.empty()) return current;
            bool digits = text.size() <= 9 && text.find_first_not_of("0123456789") == std::string::npos;
            if (digits) {
                int value = std::stoi(text);
                if (value >= min && value <= max) return value;
            }
            c::fail("Enter a whole number from " + std::to_string(min) + " to " + std::to_string(max) + ".");
            std::cout << "\n";
        }
    };
    std::string name = c::readLine("Name [" + p->name + "]: ");
    if (name.empty()) name = p->name;
    int newId = number("Product ID", p->id, 1, Store::kMaxProductId);
    int price = number("Price in Rs", static_cast<int>(p->price), 1, static_cast<int>(Store::kMaxPrice));
    int stock = number("Stock", p->stock, 0, Store::kMaxStock);
    store_.updateProduct(account_, id, newId, name, price, stock);
    c::ok("Saved.");
}

void AdminScreen::deleteProduct() {
    top("Products");
    c::section("Delete a product");
    int id = c::readInt("Product ID to delete: ", 1, Store::kMaxProductId);
    std::optional<Product> p = store_.findProduct(id);
    if (!p) throw StoreError("No product with ID " + std::to_string(id) + ".");
    if (!c::confirm("Delete " + p->name + "? It will also leave every cart.")) return back();
    store_.removeProduct(account_, id);
    c::ok("Deleted " + p->name + ".");
}

void AdminScreen::searchProducts() {
    top("Products");
    c::section("Search products");
    int how = c::menu({{1, "By ID", ""}, {2, "By name", ""}, {0, "Back", ""}});
    if (how == 0) return back();
    std::cout << "\n";
    if (how == 1) {
        int id = c::readInt("Product ID: ", 1, Store::kMaxProductId);
        std::optional<Product> p = store_.findProduct(id);
        std::cout << "\n";
        if (!p) throw StoreError("No product with ID " + std::to_string(id) + ".");
        showProducts({*p});
    } else {
        std::string text = c::readLine("Name contains: ");
        std::cout << "\n";
        showProducts(store_.searchProducts(text));
    }
}

void AdminScreen::orderQueue() {
    top("Orders");
    c::section("Order queue");
    std::vector<Order> pending = store_.pendingOrders();
    if (pending.empty()) {
        c::note("No orders are waiting.");
        return;
    }
    std::vector<std::vector<std::string>> rows;
    int position = 1;
    for (const Order& o : pending) {
        int units = 0;
        for (const OrderLine& l : o.lines) units += l.quantity;
        rows.push_back({std::to_string(position++), "#" + std::to_string(o.id), o.customer, when(o.placedAt),
                        std::to_string(units), c::money(o.bill.total)});
    }
    c::table({{"", 2, true}, {"Order", 7}, {"Customer", 12}, {"Placed", 17}, {"Items", 5, true}, {"Total", 11, true}}, rows);
}

void AdminScreen::processNext() {
    if (store_.pendingOrders().empty()) {
        top("Orders");
        c::section("Process next order");
        c::note("No orders are waiting.");
        return;
    }
    Order order = store_.processNextOrder(account_);
    top("Orders");  // drawn after processing, so the header count is current
    c::section("Process next order");
    std::cout << "  Customer: " << order.customer << "\n";
    showReceipt(order);
    c::ok("Order #" + std::to_string(order.id) + " marked as delivered.");
}

void AdminScreen::complaints() {
    top("Complaints");
    c::section("Complaints, newest first");
    std::vector<Complaint> list = store_.complaints();
    if (list.empty()) {
        c::note("No complaints.");
        return;
    }
    for (const Complaint& cpl : list) {
        std::cout << "  #" << cpl.id << "  " << cpl.customer << "  " << when(cpl.createdAt) << "  "
                  << (cpl.resolved ? "[resolved]" : "[open]") << "\n     " << cpl.text << "\n\n";
    }
    int id = c::readInt("Complaint number to mark resolved (0 to go back): ", 0, 999999999);
    if (id == 0) return back();
    store_.resolveComplaint(account_, id);
    c::ok("Complaint #" + std::to_string(id) + " resolved.");
}

void AdminScreen::customers() {
    top("Customers");
    c::section("Customers");
    std::vector<const Account*> list = store_.customers();
    if (list.empty()) {
        c::note("No customers yet.");
        return;
    }
    std::vector<std::vector<std::string>> rows;
    for (const Account* a : list) {
        std::vector<Order> orders = store_.ordersOf(*a);
        Money spent = 0;
        for (const Order& o : orders) spent += o.bill.total;
        rows.push_back({a->username(), std::to_string(orders.size()), c::money(spent)});
    }
    c::table({{"Customer", 20}, {"Orders", 6, true}, {"Spent", 14, true}}, rows);
}

void AdminScreen::stats() {
    top("Stats");
    c::section("Store stats");
    StoreStats s = store_.stats();
    auto row = [](const std::string& label, const std::string& value) {
        std::cout << "  " << label << std::string(label.size() < 30 ? 30 - label.size() : 1, ' ') << value << "\n";
    };
    row("Products", std::to_string(s.products));
    row("Units in stock", std::to_string(s.unitsInStock));
    row("Stock value", c::money(s.stockValue));
    row("Orders waiting", std::to_string(s.pendingOrders));
    row("Open complaints", std::to_string(s.openComplaints));
    row("Customers", std::to_string(s.customers));
}

void AdminScreen::resetPassword() {
    top("Customers");
    c::section("Reset a customer password");
    c::hint("Use this when a customer forgets their password. It also unlocks the account.");
    std::cout << "\n";
    std::string name = c::readLine("Customer username: ");
    c::hint(passwordRules());
    std::string password = c::readPassword("New password: ");
    if (c::readPassword("Repeat new password: ") != password) throw StoreError("The passwords don't match.");
    store_.resetPassword(account_, name, password);
    c::ok("Password reset. Give it to " + Store::normalizeUsername(name) + " privately.");
}

// ---------------------------------------------------------------- app

int App::run() {
    try {
        if (store_.needsSetup()) setup();
        while (true) {
            c::clear();
            c::header({}, "terminal shop");
            c::section("Welcome to Draz");
            c::hint(plural(static_cast<long long>(store_.products().size()), "product") + " in store");
            std::cout << "\n";
            int choice = c::menu({{1, "Shop", "log in or create an account"}, {2, "Admin panel", ""}, {0, "Exit", ""}});
            if (choice == 0) break;
            gate(choice == 1 ? Role::Customer : Role::Admin);
        }
    } catch (const c::InputClosed&) {
        std::cout << "\n";
    }
    std::cout << "\n  Thanks for shopping at Draz.\n\n";
    return 0;
}

void App::setup() {
    c::clear();
    c::header({"Setup"});
    c::section("First run");
    std::cout << "  There is no admin account yet. Create one now. It manages products,\n"
                 "  processes orders and resets customer passwords.\n\n";
    const Account* admin = nullptr;
    while (!admin) {
        try {
            std::string name = c::readLine("Admin username: ");
            c::hint(passwordRules());
            std::string password = c::readPassword("Admin password: ");
            if (c::readPassword("Repeat password: ") != password) throw StoreError("The passwords don't match.");
            admin = &store_.createFirstAdmin(name, password);
            c::ok("Admin account created.");
        } catch (const StoreError& e) {
            c::fail(e.what());
            std::cout << "\n";
        }
    }
    if (store_.products().empty()) {
        std::cout << "\n";
        if (c::confirm("Add 12 sample products to start with?")) {
            store_.addSampleProducts(*admin);
            c::ok("Added 12 sample products.");
        }
    }
    c::pause();
}

void App::gate(Role role) {
    while (true) {
        c::clear();
        c::header({role == Role::Admin ? "Admin" : "Shop"});
        c::section(role == Role::Admin ? "Admin sign in" : "Sign in to shop");
        std::vector<c::MenuItem> options = {{1, "Log in", ""}};
        if (role == Role::Customer) options.push_back({2, "Create an account", ""});
        options.push_back({3, "Forgot password", ""});
        options.push_back({0, "Back", ""});
        int choice = c::menu(options);
        if (choice == 0) return;
        try {
            if (choice == 1) {
                login(role);
                continue;  // the signed-in screen handles its own pauses
            }
            if (choice == 2) registerCustomer();
            if (choice == 3) forgotPassword(role);
        } catch (const StoreError& e) {
            c::fail(e.what());
        }
        c::pause();
    }
}

void App::login(Role role) {
    std::cout << "\n";
    std::string name = c::readLine("Username: ");
    std::string password = c::readPassword("Password: ");
    LoginResult result = store_.login(role, name, password);

    if (result.status == LoginStatus::Locked) {
        std::int64_t minutes = (result.secondsLocked + 59) / 60;
        c::fail("Too many wrong passwords. Try again in " + plural(minutes, "minute") + ".");
        c::pause();
        return;
    }
    if (result.status != LoginStatus::Ok) {
        c::fail("Wrong username or password.");
        c::pause();
        return;
    }
    if (role == Role::Admin) {
        AdminScreen(store_, *result.account).run();
    } else {
        CustomerScreen(store_, *result.account).run();
    }
}

void App::registerCustomer() {
    std::cout << "\n";
    c::hint("Username: 3 to 32 letters, digits, '.', '_' or '-'.");
    std::string name = c::readLine("Username: ");
    Store::checkUsername(Store::normalizeUsername(name));
    c::hint(passwordRules());
    std::string password = c::readPassword("Password: ");
    if (c::readPassword("Repeat password: ") != password) throw StoreError("The passwords don't match.");
    store_.registerCustomer(name, password);
    c::ok("Account created. You can log in now.");
}

void App::forgotPassword(Role role) {
    std::cout << "\n";
    c::note("Passwords are stored as salted one-way hashes, so nobody can look yours up.");
    if (role == Role::Customer) {
        c::hint("Ask the store admin to reset it, then change it after you log in.");
    } else {
        c::hint("An admin password can't be reset from inside the app. Keep it in a password manager.");
    }
}

}  // namespace draz
