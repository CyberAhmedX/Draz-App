#include <exception>

#include "check.h"

int main() {
    int crashed = 0;
    for (const test::Case& c : test::cases()) {
        int before = test::failures();
        try {
            c.body();
        } catch (const std::exception& e) {
            ++crashed;
            std::cerr << "  FAIL unexpected exception: " << e.what() << "\n";
        }
        bool ok = test::failures() == before;
        std::cout << (ok ? "  ok   " : "  FAIL ") << c.name << "\n";
    }
    int total = static_cast<int>(test::cases().size());
    int bad = test::failures() + crashed;
    std::cout << "\n" << total << " tests, " << bad << " failed checks\n";
    return bad == 0 ? 0 : 1;
}
