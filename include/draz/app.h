#pragma once

#include <string>
#include <vector>

#include "draz/console.h"
#include "draz/store.h"

namespace draz {

// A signed-in screen. Each role has its own menu; the shared loop that
// draws it and reports errors lives in run().
class Screen {
public:
    Screen(Store& store, const Account& account) : store_(store), account_(account) {}
    virtual ~Screen() = default;

    void run();

protected:
    virtual std::vector<std::string> path() const = 0;
    virtual std::string status() const = 0;  // right side of the header bar
    virtual std::vector<console::MenuItem> items() const = 0;
    // Handles one choice. Returns false when the user logs out.
    virtual bool handle(int choice) = 0;

    void top(const std::string& page) const;  // clears and draws the header for a sub-page
    void changePassword();
    void showProducts(const std::vector<Product>& products) const;
    static void showReceipt(const Order& order);

    // Set by a page that was cancelled or already waited for the user,
    // so run() goes straight back to the menu without "Press Enter".
    void back() { skipPause_ = true; }

    Store& store_;
    const Account& account_;

private:
    bool skipPause_ = false;
};

class CustomerScreen final : public Screen {
public:
    using Screen::Screen;

protected:
    std::vector<std::string> path() const override { return {"Shop"}; }
    std::string status() const override;
    std::vector<console::MenuItem> items() const override;
    bool handle(int choice) override;

private:
    void browse();
    void sortByPrice();
    void search();
    void addToCart();
    void viewCart();
    void checkout();
    void myOrders();
    void returnItem();
    void complaints();
};

class AdminScreen final : public Screen {
public:
    using Screen::Screen;

protected:
    std::vector<std::string> path() const override { return {"Admin"}; }
    std::string status() const override;
    std::vector<console::MenuItem> items() const override;
    bool handle(int choice) override;

private:
    void allProducts();
    void addProduct();
    void editProduct();
    void deleteProduct();
    void searchProducts();
    void orderQueue();
    void processNext();
    void complaints();
    void customers();
    void stats();
    void resetPassword();
};

// The welcome screen, the two sign-in gates and first-run setup.
class App {
public:
    explicit App(Store& store) : store_(store) {}
    int run();

private:
    void setup();
    void gate(Role role);
    void login(Role role);
    void registerCustomer();
    void forgotPassword(Role role);

    Store& store_;
};

}  // namespace draz
