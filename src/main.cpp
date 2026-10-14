#include <exception>
#include <iostream>
#include <string>

#include "draz/app.h"
#include "draz/console.h"
#include "draz/store.h"

namespace {

void usage() {
    std::cout << "Usage: draz [--data <folder>]\n\n"
                 "  --data <folder>  where accounts, products and orders are kept (default: data)\n"
                 "  --help           show this message\n";
}

}  // namespace

int main(int argc, char** argv) {
    draz::Store::Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        if (arg == "--data" && i + 1 < argc) {
            options.dataDir = argv[++i];
            continue;
        }
        std::cerr << "Unknown option: " << arg << "\n\n";
        usage();
        return 2;
    }

    draz::console::init();
    try {
        draz::Store store(options);
        return draz::App(store).run();
    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << "\n";
        return 1;
    }
}
