#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "draz/crypto.h"
#include "draz/ds/bst.h"
#include "draz/ds/queue.h"
#include "draz/ds/stack.h"

namespace draz {

using Money = std::int64_t;  // whole rupees

// Thrown for anything the person using the store did wrong, such as an
// out-of-stock product or a taken username. The message is safe to show.
class StoreError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class Role { Admin, Customer };
std::string roleName(Role role);

// A salted PBKDF2-SHA256 password hash. The password itself is never stored.
struct PasswordHash {
    std::uint32_t iterations = 0;
    crypto::Bytes salt;
    crypto::Bytes hash;

    static PasswordHash create(const std::string& password, std::uint32_t iterations);
    bool matches(const std::string& password) const;
    std::string encode() const;  // "pbkdf2-sha256$<iterations>$<salt hex>$<hash hex>"
    static std::optional<PasswordHash> decode(const std::string& text);
};

class Account {
public:
    Account(std::string username, PasswordHash password)
        : username_(std::move(username)), password_(std::move(password)) {}
    virtual ~Account() = default;

    virtual Role role() const = 0;

    const std::string& username() const { return username_; }
    const PasswordHash& password() const { return password_; }
    void setPassword(PasswordHash password) { password_ = std::move(password); }

    int failedLogins = 0;
    std::int64_t lockedUntil = 0;  // Unix time, 0 when not locked

private:
    std::string username_;
    PasswordHash password_;
};

class Admin final : public Account {
public:
    using Account::Account;
    Role role() const override { return Role::Admin; }
};

class Customer final : public Account {
public:
    using Account::Account;
    Role role() const override { return Role::Customer; }
};

struct Product {
    int id = 0;
    std::string name;
    Money price = 0;
    int stock = 0;
};

struct CartItem {
    int productId = 0;
    std::string name;
    Money price = 0;  // price when it was added; checkout uses the current price
    int quantity = 0;
};

struct Bill {
    Money subtotal = 0;
    int discountPercent = 0;
    Money discount = 0;
    Money total = 0;
};

// Same discount tiers as the original project.
int discountPercentFor(Money subtotal);
Bill makeBill(Money subtotal);

enum class OrderStatus { Pending, Delivered };

struct OrderLine {
    int productId = 0;
    std::string name;
    Money price = 0;
    int quantity = 0;
    int returned = 0;
};

struct Order {
    int id = 0;
    std::string customer;
    std::int64_t placedAt = 0;
    OrderStatus status = OrderStatus::Pending;
    std::vector<OrderLine> lines;
    Bill bill;
};

struct Complaint {
    int id = 0;
    std::string customer;
    std::int64_t createdAt = 0;
    bool resolved = false;
    std::string text;
};

enum class LoginStatus { Ok, WrongCredentials, Locked };

struct LoginResult {
    LoginStatus status = LoginStatus::WrongCredentials;
    const Account* account = nullptr;
    std::int64_t secondsLocked = 0;
};

struct StoreStats {
    std::size_t products = 0;
    int treeHeight = 0;
    long long unitsInStock = 0;
    Money stockValue = 0;
    std::size_t pendingOrders = 0;
    std::size_t openComplaints = 0;
    std::size_t customers = 0;
};

class Store {
public:
    struct Options {
        std::filesystem::path dataDir = "data";
        std::uint32_t iterations = 600000;  // OWASP guidance for PBKDF2-SHA256
        int maxFailedLogins = 5;
        std::int64_t lockSeconds = 300;
        std::function<std::int64_t()> clock;  // Unix time; defaults to the system clock
    };

    static constexpr int kMinPassword = 8;
    static constexpr int kMaxPassword = 128;
    static constexpr int kMaxProductId = 999999;
    static constexpr Money kMaxPrice = 10000000;
    static constexpr int kMaxStock = 100000;
    static constexpr int kMaxPerLine = 99;
    static constexpr int kMaxComplaint = 300;

    explicit Store(Options options);

    // ---- accounts
    bool needsSetup() const;
    const Account& createFirstAdmin(const std::string& username, const std::string& password);
    void registerCustomer(const std::string& username, const std::string& password);
    LoginResult login(Role role, const std::string& username, const std::string& password);
    void changePassword(const Account& who, const std::string& current, const std::string& next);
    void resetPassword(const Account& admin, const std::string& username, const std::string& password);
    std::vector<const Account*> customers() const;

    // ---- products (changes are admin only)
    void addProduct(const Account& admin, int id, const std::string& name, Money price, int stock);
    void updateProduct(const Account& admin, int id, int newId, const std::string& name, Money price, int stock);
    void removeProduct(const Account& admin, int id);
    void addSampleProducts(const Account& admin);
    std::optional<Product> findProduct(int id) const;
    std::vector<Product> products() const;  // sorted by ID (in-order traversal)
    std::vector<Product> productsByPrice(bool lowestFirst) const;
    std::vector<Product> searchProducts(const std::string& text) const;
    StoreStats stats() const;

    // ---- cart (one per customer, kept between sessions)
    void addToCart(const Account& customer, int productId, int quantity);
    std::optional<CartItem> removeLastAdded(const Account& customer);
    void removeFromCart(const Account& customer, int productId);
    std::vector<CartItem> cart(const Account& customer) const;  // most recent first
    Bill cartBill(const Account& customer) const;

    // ---- orders
    Order checkout(const Account& customer);
    std::vector<Order> pendingOrders() const;  // oldest first, as they are in the queue
    Order processNextOrder(const Account& admin);
    std::vector<Order> ordersOf(const Account& customer) const;  // newest first
    Money returnItem(const Account& customer, int orderId, int productId, int quantity);

    // ---- complaints
    void fileComplaint(const Account& customer, const std::string& text);
    std::vector<Complaint> complaints() const;  // newest first
    std::vector<Complaint> complaintsOf(const Account& customer) const;
    void resolveComplaint(const Account& admin, int id);

    // ---- input rules, shared with the screens
    static std::string normalizeUsername(const std::string& username);
    static void checkUsername(const std::string& username);
    static void checkPassword(const std::string& username, const std::string& password);
    static std::string cleanText(const std::string& text, std::size_t min, std::size_t max, const std::string& what);

private:
    std::int64_t now() const;
    Account* findAccount(const std::string& username);
    static void requireRole(const Account& who, Role role);
    static void checkProductFields(int id, Money price, int stock);
    Order* findOrder(int id);

    void load();
    void saveUsers() const;
    void saveProducts() const;
    void saveCarts() const;
    void saveOrders() const;
    void saveComplaints() const;

    Options options_;
    std::vector<std::unique_ptr<Account>> accounts_;
    ds::BST<int, Product> products_;
    std::map<std::string, ds::Stack<CartItem>> carts_;
    std::vector<Order> orders_;     // every order, by ascending ID
    ds::Queue<int> pending_;        // IDs of orders waiting to be processed
    ds::Stack<Complaint> complaints_;
    int nextOrderId_ = 1001;
    int nextComplaintId_ = 1;
    std::optional<PasswordHash> dummy_;  // checked against unknown usernames, see login()
};

}  // namespace draz
