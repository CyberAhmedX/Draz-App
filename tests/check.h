#pragma once

// A very small test framework: TEST registers a function, CHECK records a
// failure without stopping the test, and main() runs everything.

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    std::function<void()> body;
};

inline std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, std::function<void()> body) { cases().push_back({name, std::move(body)}); }
};

inline void fail(const char* file, int line, const std::string& what) {
    ++failures();
    std::cerr << "  FAIL " << file << ":" << line << ": " << what << "\n";
}

}  // namespace test

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                                 \
    static void TEST_CAT(test_fn_, __LINE__)();                                    \
    static test::Register TEST_CAT(test_reg_, __LINE__)(name, TEST_CAT(test_fn_, __LINE__)); \
    static void TEST_CAT(test_fn_, __LINE__)()

#define CHECK(...)                                                      \
    do {                                                                \
        if (!(__VA_ARGS__)) test::fail(__FILE__, __LINE__, #__VA_ARGS__); \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        const auto check_a_ = (a);                                                        \
        const auto check_b_ = (b);                                                        \
        if (!(check_a_ == check_b_)) test::fail(__FILE__, __LINE__, #a " == " #b);      \
    } while (0)

// Passes when the expression throws ExceptionType.
#define CHECK_THROWS(ExceptionType, expr)                                               \
    do {                                                                                \
        bool check_thrown_ = false;                                                     \
        try {                                                                           \
            (void)(expr);                                                               \
        } catch (const ExceptionType&) {                                                \
            check_thrown_ = true;                                                       \
        }                                                                               \
        if (!check_thrown_) test::fail(__FILE__, __LINE__, #expr " should throw " #ExceptionType); \
    } while (0)
