#include "draz/store.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#include "draz/ds/sort.h"
#include "draz/storage.h"

namespace draz {

namespace {

constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::optional<std::int64_t> parseLong(const std::string& text) {
    if (text.empty() || text.size() > 18) return std::nullopt;
    std::size_t start = text[0] == '-' ? 1 : 0;
    if (start == text.size()) return std::nullopt;
    for (std::size_t i = start; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return std::nullopt;
    }
    return std::stoll(text);
}

std::optional<int> parseInt(const std::string& text) {
    std::optional<std::int64_t> value = parseLong(text);
    if (!value || *value < -2147483647LL || *value > 2147483647LL) return std::nullopt;
    return static_cast<int>(*value);
}

std::string str(std::int64_t value) { return std::to_string(value); }

}  // namespace

std::string roleName(Role role) { return role == Role::Admin ? "admin" : "customer"; }

// ---------------------------------------------------------------- money

int discountPercentFor(Money subtotal) {
    if (subtotal < 1000) return 0;
    if (subtotal < 5000) return 5;
    if (subtotal < 10000) return 7;
    if (subtotal < 20000) return 12;
    return 20;
}

Bill makeBill(Money subtotal) {
    Bill bill;
    bill.subtotal = subtotal;
    bill.discountPercent = discountPercentFor(subtotal);
    bill.discount = subtotal * bill.discountPercent / 100;
    bill.total = subtotal - bill.discount;
    return bill;
}

// ---------------------------------------------------------------- passwords

PasswordHash PasswordHash::create(const std::string& password, std::uint32_t iterations) {
    PasswordHash result;
    result.iterations = iterations;
    result.salt = crypto::randomBytes(kSaltBytes);
    result.hash = crypto::pbkdf2Sha256(password, result.salt, iterations, kHashBytes);
    return result;
}

bool PasswordHash::matches(const std::string& password) const {
    if (iterations == 0 || hash.empty()) return false;
    return crypto::constantTimeEqual(crypto::pbkdf2Sha256(password, salt, iterations, hash.size()), hash);
}

std::string PasswordHash::encode() const {
    return "pbkdf2-sha256$" + std::to_string(iterations) + "$" + crypto::toHex(salt) + "$" + crypto::toHex(hash);
}

std::optional<PasswordHash> PasswordHash::decode(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t pos; (pos = text.find('$', start)) != std::string::npos; start = pos + 1) {
        parts.push_back(text.substr(start, pos - start));
    }
    parts.push_back(text.substr(start));
    if (parts.size() != 4 || parts[0] != "pbkdf2-sha256") return std::nullopt;

    std::optional<int> iterations = parseInt(parts[1]);
    PasswordHash result;
    result.salt = crypto::fromHex(parts[2]);
    result.hash = crypto::fromHex(parts[3]);
    if (!iterations || *iterations < 1 || result.salt.empty() || result.hash.empty()) return std::nullopt;
    result.iterations = static_cast<std::uint32_t>(*iterations);
    return result;
}

// ---------------------------------------------------------------- input rules

std::string Store::normalizeUsername(const std::string& username) { return lower(username); }

void Store::checkUsername(const std::string& username) {
    if (username.size() < 3 || username.size() > 32) throw StoreError("Username must be 3 to 32 characters.");
    for (char c : username) {
        bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
        if (!ok) throw StoreError("Username can only use letters, digits, '.', '_' and '-'.");
    }
}

void Store::checkPassword(const std::string& username, const std::string& password) {
    if (password.size() < static_cast<std::size_t>(kMinPassword)) {
        throw StoreError("Password must be at least " + std::to_string(kMinPassword) + " characters.");
    }
    if (password.size() > static_cast<std::size_t>(kMaxPassword)) {
        throw StoreError("Password must be at most " + std::to_string(kMaxPassword) + " characters.");
    }
    if (lower(password) == lower(username)) throw StoreError("Password can't be the same as the username.");
}

// Trims, squeezes runs of spaces and rejects control characters.
std::string Store::cleanText(const std::string& text, std::size_t min, std::size_t max, const std::string& what) {
    std::string out;
    bool space = false;
    for (char c : text) {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isspace(u)) {
            space = !out.empty();
            continue;
        }
        if (u < 32 || u == 127) throw StoreError(what + " has an unsupported character.");
        if (space) out.push_back(' ');
        out.push_back(c);
        space = false;
    }
    if (out.size() < min || out.size() > max) {
        throw StoreError(what + " must be " + std::to_string(min) + " to " + std::to_string(max) + " characters.");
    }
    return out;
}

void Store::checkProductFields(int id, Money price, int stock) {
    if (id < 1 || id > kMaxProductId) throw StoreError("Product ID must be 1 to " + str(kMaxProductId) + ".");
    if (price < 1 || price > kMaxPrice) throw StoreError("Price must be Rs 1 to Rs " + str(kMaxPrice) + ".");
    if (stock < 0 || stock > kMaxStock) throw StoreError("Stock must be 0 to " + str(kMaxStock) + ".");
}

void Store::requireRole(const Account& who, Role role) {
    if (who.role() != role) throw StoreError("Only " + roleName(role) + " accounts can do that.");
}

// ---------------------------------------------------------------- setup and accounts

Store::Store(Options options) : options_(std::move(options)) {
    if (!options_.clock) {
        options_.clock = [] {
            return static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count());
        };
    }
    load();
}

std::int64_t Store::now() const { return options_.clock(); }

Account* Store::findAccount(const std::string& username) {
    for (auto& a : accounts_) {
        if (a->username() == username) return a.get();
    }
    return nullptr;
}

bool Store::needsSetup() const {
    return std::none_of(accounts_.begin(), accounts_.end(), [](const auto& a) { return a->role() == Role::Admin; });
}

const Account& Store::createFirstAdmin(const std::string& username, const std::string& password) {
    if (!needsSetup()) throw StoreError("An admin account already exists.");
    std::string name = normalizeUsername(username);
    checkUsername(name);
    checkPassword(name, password);
    accounts_.push_back(std::make_unique<Admin>(name, PasswordHash::create(password, options_.iterations)));
    saveUsers();
    return *accounts_.back();
}

void Store::registerCustomer(const std::string& username, const std::string& password) {
    std::string name = normalizeUsername(username);
    checkUsername(name);
    checkPassword(name, password);
    if (findAccount(name)) throw StoreError("That username is taken.");
    accounts_.push_back(std::make_unique<Customer>(name, PasswordHash::create(password, options_.iterations)));
    saveUsers();
}

LoginResult Store::login(Role role, const std::string& username, const std::string& password) {
    Account* account = findAccount(normalizeUsername(username));
    if (!account || account->role() != role) {
        // Hash anyway, so an unknown username takes as long as a wrong
        // password and the response time doesn't reveal which accounts exist.
        if (!dummy_) dummy_ = PasswordHash::create("not-a-real-password", options_.iterations);
        dummy_->matches(password);
        return {LoginStatus::WrongCredentials, nullptr, 0};
    }

    std::int64_t current = now();
    if (account->lockedUntil > current) return {LoginStatus::Locked, nullptr, account->lockedUntil - current};

    if (!account->password().matches(password)) {
        if (++account->failedLogins >= options_.maxFailedLogins) {
            account->failedLogins = 0;
            account->lockedUntil = current + options_.lockSeconds;
            saveUsers();
            return {LoginStatus::Locked, nullptr, options_.lockSeconds};
        }
        saveUsers();
        return {LoginStatus::WrongCredentials, nullptr, 0};
    }

    if (account->failedLogins || account->lockedUntil) {
        account->failedLogins = 0;
        account->lockedUntil = 0;
        saveUsers();
    }
    return {LoginStatus::Ok, account, 0};
}

void Store::changePassword(const Account& who, const std::string& current, const std::string& next) {
    Account* target = findAccount(who.username());
    if (!target) throw StoreError("Account not found.");
    if (!target->password().matches(current)) throw StoreError("Current password is wrong.");
    checkPassword(target->username(), next);
    target->setPassword(PasswordHash::create(next, options_.iterations));
    saveUsers();
}

void Store::resetPassword(const Account& admin, const std::string& username, const std::string& password) {
    requireRole(admin, Role::Admin);
    Account* target = findAccount(normalizeUsername(username));
    if (!target || target->role() != Role::Customer) throw StoreError("No customer with that username.");
    checkPassword(target->username(), password);
    target->setPassword(PasswordHash::create(password, options_.iterations));
    target->failedLogins = 0;
    target->lockedUntil = 0;
    saveUsers();
}

std::vector<const Account*> Store::customers() const {
    std::vector<const Account*> out;
    for (const auto& a : accounts_) {
        if (a->role() == Role::Customer) out.push_back(a.get());
    }
    ds::mergeSort(out, [](const Account* a, const Account* b) { return a->username() < b->username(); });
    return out;
}

// ---------------------------------------------------------------- products

void Store::addProduct(const Account& admin, int id, const std::string& name, Money price, int stock) {
    requireRole(admin, Role::Admin);
    checkProductFields(id, price, stock);
    std::string clean = cleanText(name, 2, 40, "Product name");
    if (!products_.insert(id, Product{id, clean, price, stock})) {
        throw StoreError("Product ID " + str(id) + " is already used by " + products_.find(id)->name + ".");
    }
    saveProducts();
}

void Store::updateProduct(const Account& admin, int id, int newId, const std::string& name, Money price, int stock) {
    requireRole(admin, Role::Admin);
    const Product* current = products_.find(id);
    if (!current) throw StoreError("No product with ID " + str(id) + ".");
    checkProductFields(newId, price, stock);
    std::string clean = cleanText(name, 2, 40, "Product name");

    if (newId == id) {
        *products_.find(id) = Product{id, clean, price, stock};
    } else {
        if (products_.contains(newId)) {
            throw StoreError("Product ID " + str(newId) + " is already used by " + products_.find(newId)->name + ".");
        }
        // A new ID means a new place in the tree: remove and insert again,
        // instead of editing the key in place and breaking the ordering.
        products_.erase(id);
        products_.insert(newId, Product{newId, clean, price, stock});
        for (auto& [_, cart] : carts_) {
            if (CartItem* item = cart.findFirst([&](const CartItem& c) { return c.productId == id; })) {
                item->productId = newId;
            }
        }
        saveCarts();
    }
    saveProducts();
}

void Store::removeProduct(const Account& admin, int id) {
    requireRole(admin, Role::Admin);
    if (!products_.erase(id)) throw StoreError("No product with ID " + str(id) + ".");
    for (auto& [_, cart] : carts_) cart.removeFirst([&](const CartItem& c) { return c.productId == id; });
    saveProducts();
    saveCarts();
}

void Store::addSampleProducts(const Account& admin) {
    requireRole(admin, Role::Admin);
    const Product samples[] = {
        {101, "Wireless Earbuds", 2499, 40},   {102, "Power Bank 10000mAh", 3299, 25},
        {103, "USB-C Cable 1m", 399, 120},     {104, "Smart Watch", 6999, 15},
        {105, "Bluetooth Speaker", 4250, 20},  {106, "Laptop Backpack", 2850, 30},
        {107, "Ceramic Mug", 450, 60},         {108, "Cotton Kurta", 1899, 35},
        {109, "Running Shoes", 5499, 18},      {110, "LED Desk Lamp", 1650, 22},
        {111, "Mechanical Keyboard", 8999, 10}, {112, "Gaming Mouse", 2199, 28},
    };
    for (const Product& p : samples) products_.insert(p.id, p);
    products_.rebalance();
    saveProducts();
}

std::optional<Product> Store::findProduct(int id) const {
    const Product* p = products_.find(id);
    return p ? std::optional<Product>(*p) : std::nullopt;
}

std::vector<Product> Store::products() const {
    std::vector<Product> out;
    out.reserve(products_.size());
    products_.inorder([&](const int&, const Product& p) { out.push_back(p); });
    return out;
}

std::vector<Product> Store::productsByPrice(bool lowestFirst) const {
    std::vector<Product> out = products();
    ds::mergeSort(out, [lowestFirst](const Product& a, const Product& b) {
        return lowestFirst ? a.price < b.price : b.price < a.price;
    });
    return out;
}

std::vector<Product> Store::searchProducts(const std::string& text) const {
    std::string needle = lower(text);
    std::vector<Product> out;
    if (needle.empty()) return out;
    products_.inorder([&](const int&, const Product& p) {
        if (lower(p.name).find(needle) != std::string::npos) out.push_back(p);
    });
    return out;
}

StoreStats Store::stats() const {
    StoreStats s;
    s.products = products_.size();
    s.treeHeight = products_.height();
    products_.inorder([&](const int&, const Product& p) {
        s.unitsInStock += p.stock;
        s.stockValue += p.price * p.stock;
    });
    s.pendingOrders = pending_.size();
    complaints_.forEach([&](const Complaint& c) { s.openComplaints += c.resolved ? 0 : 1; });
    s.customers = customers().size();
    return s;
}

// ---------------------------------------------------------------- cart

void Store::addToCart(const Account& customer, int productId, int quantity) {
    requireRole(customer, Role::Customer);
    const Product* product = products_.find(productId);
    if (!product) throw StoreError("No product with ID " + str(productId) + ".");
    if (quantity < 1 || quantity > kMaxPerLine) throw StoreError("Quantity must be 1 to " + str(kMaxPerLine) + ".");

    ds::Stack<CartItem>& cart = carts_[customer.username()];
    CartItem* existing = cart.findFirst([&](const CartItem& c) { return c.productId == productId; });
    int inCart = existing ? existing->quantity : 0;
    if (inCart + quantity > product->stock) {
        if (product->stock == 0) throw StoreError(product->name + " is out of stock.");
        throw StoreError("Only " + str(product->stock) + " of " + product->name + " in stock" +
                         (inCart ? " and " + str(inCart) + " are already in your cart." : "."));
    }
    if (inCart + quantity > kMaxPerLine) throw StoreError("You can have at most " + str(kMaxPerLine) + " of one product.");

    // Stock is only taken at checkout. Leaving items in the cart doesn't
    // remove them from the store.
    if (existing) {
        CartItem item = *existing;
        cart.removeFirst([&](const CartItem& c) { return c.productId == productId; });
        item.quantity += quantity;
        item.price = product->price;
        cart.push(item);  // most recently touched goes on top
    } else {
        cart.push(CartItem{productId, product->name, product->price, quantity});
    }
    saveCarts();
}

std::optional<CartItem> Store::removeLastAdded(const Account& customer) {
    requireRole(customer, Role::Customer);
    auto it = carts_.find(customer.username());
    if (it == carts_.end() || it->second.empty()) return std::nullopt;
    CartItem item = it->second.pop();
    saveCarts();
    return item;
}

void Store::removeFromCart(const Account& customer, int productId) {
    requireRole(customer, Role::Customer);
    auto it = carts_.find(customer.username());
    if (it == carts_.end() || !it->second.removeFirst([&](const CartItem& c) { return c.productId == productId; })) {
        throw StoreError("That product isn't in your cart.");
    }
    saveCarts();
}

std::vector<CartItem> Store::cart(const Account& customer) const {
    std::vector<CartItem> out;
    auto it = carts_.find(customer.username());
    if (it == carts_.end()) return out;
    it->second.forEach([&](const CartItem& c) {
        CartItem item = c;
        if (const Product* p = products_.find(c.productId)) {
            item.name = p->name;
            item.price = p->price;
        }
        out.push_back(item);
    });
    return out;
}

Bill Store::cartBill(const Account& customer) const {
    Money subtotal = 0;
    for (const CartItem& item : cart(customer)) subtotal += item.price * item.quantity;
    return makeBill(subtotal);
}

// ---------------------------------------------------------------- orders

Order Store::checkout(const Account& customer) {
    requireRole(customer, Role::Customer);
    std::vector<CartItem> items = cart(customer);
    if (items.empty()) throw StoreError("Your cart is empty.");

    // Check everything first, so an order is either placed in full or not at all.
    for (const CartItem& item : items) {
        const Product* p = products_.find(item.productId);
        if (!p) throw StoreError(item.name + " is no longer sold. Remove it from your cart.");
        if (item.quantity > p->stock) {
            throw StoreError("Only " + str(p->stock) + " of " + p->name + " left. Change your cart and try again.");
        }
    }

    Order order;
    order.id = nextOrderId_++;
    order.customer = customer.username();
    order.placedAt = now();
    Money subtotal = 0;
    for (auto it = items.rbegin(); it != items.rend(); ++it) {  // oldest cart item first
        products_.find(it->productId)->stock -= it->quantity;
        order.lines.push_back({it->productId, it->name, it->price, it->quantity, 0});
        subtotal += it->price * it->quantity;
    }
    order.bill = makeBill(subtotal);

    orders_.push_back(order);
    pending_.enqueue(order.id);
    carts_[customer.username()].clear();
    saveProducts();
    saveOrders();
    saveCarts();
    return order;
}

Order* Store::findOrder(int id) {
    for (Order& o : orders_) {
        if (o.id == id) return &o;
    }
    return nullptr;
}

std::vector<Order> Store::pendingOrders() const {
    std::vector<Order> out;
    pending_.forEach([&](const int& id) {
        for (const Order& o : orders_) {
            if (o.id == id) out.push_back(o);
        }
    });
    return out;
}

Order Store::processNextOrder(const Account& admin) {
    requireRole(admin, Role::Admin);
    if (pending_.empty()) throw StoreError("There are no orders waiting.");
    Order* order = findOrder(pending_.dequeue());
    order->status = OrderStatus::Delivered;
    saveOrders();
    return *order;
}

std::vector<Order> Store::ordersOf(const Account& customer) const {
    std::vector<Order> out;
    for (auto it = orders_.rbegin(); it != orders_.rend(); ++it) {
        if (it->customer == customer.username()) out.push_back(*it);
    }
    return out;
}

Money Store::returnItem(const Account& customer, int orderId, int productId, int quantity) {
    requireRole(customer, Role::Customer);
    Order* order = findOrder(orderId);
    if (!order || order->customer != customer.username()) throw StoreError("You have no order #" + str(orderId) + ".");
    if (order->status != OrderStatus::Delivered) throw StoreError("Order #" + str(orderId) + " hasn't been delivered yet.");

    for (OrderLine& line : order->lines) {
        if (line.productId != productId) continue;
        int left = line.quantity - line.returned;
        if (left == 0) throw StoreError("You already returned all of " + line.name + ".");
        if (quantity < 1 || quantity > left) throw StoreError("You can return 1 to " + str(left) + " of " + line.name + ".");

        line.returned += quantity;
        if (Product* p = products_.find(productId)) p->stock = std::min(p->stock + quantity, kMaxStock);
        // Refund what was paid: the price minus the order's discount.
        Money gross = line.price * quantity;
        Money refund = gross - gross * order->bill.discountPercent / 100;
        saveOrders();
        saveProducts();
        return refund;
    }
    throw StoreError("That product isn't in order #" + str(orderId) + ".");
}

// ---------------------------------------------------------------- complaints

void Store::fileComplaint(const Account& customer, const std::string& text) {
    requireRole(customer, Role::Customer);
    std::string clean = cleanText(text, 5, kMaxComplaint, "Complaint");
    complaints_.push(Complaint{nextComplaintId_++, customer.username(), now(), false, clean});
    saveComplaints();
}

std::vector<Complaint> Store::complaints() const {
    std::vector<Complaint> out;
    complaints_.forEach([&](const Complaint& c) { out.push_back(c); });
    return out;
}

std::vector<Complaint> Store::complaintsOf(const Account& customer) const {
    std::vector<Complaint> out;
    complaints_.forEach([&](const Complaint& c) {
        if (c.customer == customer.username()) out.push_back(c);
    });
    return out;
}

void Store::resolveComplaint(const Account& admin, int id) {
    requireRole(admin, Role::Admin);
    Complaint* c = complaints_.findFirst([&](const Complaint& x) { return x.id == id; });
    if (!c) throw StoreError("No complaint #" + str(id) + ".");
    if (c->resolved) throw StoreError("Complaint #" + str(id) + " is already resolved.");
    c->resolved = true;
    saveComplaints();
}

// ---------------------------------------------------------------- files

void Store::load() {
    const std::filesystem::path& dir = options_.dataDir;

    for (const auto& row : storage::readTable(dir / "users.tsv")) {
        if (row.size() != 5 || (row[0] != "admin" && row[0] != "customer") || findAccount(row[1])) continue;
        std::optional<PasswordHash> hash = PasswordHash::decode(row[2]);
        if (!hash) continue;
        std::unique_ptr<Account> account;
        if (row[0] == "admin") {
            account = std::make_unique<Admin>(row[1], *hash);
        } else {
            account = std::make_unique<Customer>(row[1], *hash);
        }
        account->failedLogins = parseInt(row[3]).value_or(0);
        account->lockedUntil = parseLong(row[4]).value_or(0);
        accounts_.push_back(std::move(account));
    }

    for (const auto& row : storage::readTable(dir / "products.tsv")) {
        if (row.size() != 4) continue;
        std::optional<int> id = parseInt(row[0]), stock = parseInt(row[3]);
        std::optional<std::int64_t> price = parseLong(row[2]);
        if (id && price && stock) products_.insert(*id, Product{*id, row[1], *price, *stock});
    }
    products_.rebalance();  // the file is in ID order, which would build a chain

    for (const auto& row : storage::readTable(dir / "carts.tsv")) {  // bottom of each stack first
        if (row.size() != 5) continue;
        std::optional<int> id = parseInt(row[1]), qty = parseInt(row[4]);
        std::optional<std::int64_t> price = parseLong(row[3]);
        if (id && price && qty) carts_[row[0]].push(CartItem{*id, row[2], *price, *qty});
    }

    for (const auto& row : storage::readTable(dir / "orders.tsv")) {
        if (row.size() != 7) continue;
        std::optional<int> id = parseInt(row[0]), pct = parseInt(row[5]);
        std::optional<std::int64_t> at = parseLong(row[2]), subtotal = parseLong(row[4]), total = parseLong(row[6]);
        if (!id || !pct || !at || !subtotal || !total || (row[3] != "pending" && row[3] != "delivered")) continue;
        Order o;
        o.id = *id;
        o.customer = row[1];
        o.placedAt = *at;
        o.status = row[3] == "pending" ? OrderStatus::Pending : OrderStatus::Delivered;
        o.bill = Bill{*subtotal, *pct, *subtotal - *total, *total};
        orders_.push_back(o);
        nextOrderId_ = std::max(nextOrderId_, o.id + 1);
    }
    for (const auto& row : storage::readTable(dir / "order_lines.tsv")) {
        if (row.size() != 6) continue;
        std::optional<int> orderId = parseInt(row[0]), id = parseInt(row[1]), qty = parseInt(row[4]), ret = parseInt(row[5]);
        std::optional<std::int64_t> price = parseLong(row[3]);
        Order* o = orderId ? findOrder(*orderId) : nullptr;
        if (o && id && price && qty && ret) o->lines.push_back({*id, row[2], *price, *qty, *ret});
    }
    ds::mergeSort(orders_, [](const Order& a, const Order& b) { return a.id < b.id; });
    for (const Order& o : orders_) {
        if (o.status == OrderStatus::Pending) pending_.enqueue(o.id);
    }

    for (const auto& row : storage::readTable(dir / "complaints.tsv")) {  // oldest first
        if (row.size() != 5) continue;
        std::optional<int> id = parseInt(row[0]);
        std::optional<std::int64_t> at = parseLong(row[2]);
        if (!id || !at) continue;
        complaints_.push(Complaint{*id, row[1], *at, row[3] == "1", row[4]});
        nextComplaintId_ = std::max(nextComplaintId_, *id + 1);
    }
}

void Store::saveUsers() const {
    std::vector<storage::Row> rows;
    for (const auto& a : accounts_) {
        rows.push_back({roleName(a->role()), a->username(), a->password().encode(), str(a->failedLogins), str(a->lockedUntil)});
    }
    storage::writeTable(options_.dataDir / "users.tsv", rows);
}

void Store::saveProducts() const {
    std::vector<storage::Row> rows;
    products_.inorder([&](const int&, const Product& p) {
        rows.push_back({str(p.id), p.name, str(p.price), str(p.stock)});
    });
    storage::writeTable(options_.dataDir / "products.tsv", rows);
}

void Store::saveCarts() const {
    std::vector<storage::Row> rows;
    for (const auto& entry : carts_) {
        const std::string& customer = entry.first;
        std::vector<storage::Row> items;
        entry.second.forEach([&](const CartItem& c) {
            items.push_back({customer, str(c.productId), c.name, str(c.price), str(c.quantity)});
        });
        rows.insert(rows.end(), items.rbegin(), items.rend());  // bottom first, so loading rebuilds the same stack
    }
    storage::writeTable(options_.dataDir / "carts.tsv", rows);
}

void Store::saveOrders() const {
    std::vector<storage::Row> orders, lines;
    for (const Order& o : orders_) {
        orders.push_back({str(o.id), o.customer, str(o.placedAt), o.status == OrderStatus::Pending ? "pending" : "delivered",
                          str(o.bill.subtotal), str(o.bill.discountPercent), str(o.bill.total)});
        for (const OrderLine& l : o.lines) {
            lines.push_back({str(o.id), str(l.productId), l.name, str(l.price), str(l.quantity), str(l.returned)});
        }
    }
    storage::writeTable(options_.dataDir / "orders.tsv", orders);
    storage::writeTable(options_.dataDir / "order_lines.tsv", lines);
}

void Store::saveComplaints() const {
    std::vector<storage::Row> rows;
    complaints_.forEach([&](const Complaint& c) {
        rows.push_back({str(c.id), c.customer, str(c.createdAt), c.resolved ? "1" : "0", c.text});
    });
    std::reverse(rows.begin(), rows.end());  // oldest first
    storage::writeTable(options_.dataDir / "complaints.tsv", rows);
}

}  // namespace draz
