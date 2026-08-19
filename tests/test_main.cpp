// test_main.cpp — minimal test runner (no external test framework required).
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

struct Test {
    std::string name;
    std::function<void()> fn;
};

static std::vector<Test>& registry() {
    static std::vector<Test> r;
    return r;
}

int register_test(std::string name, std::function<void()> fn) {
    registry().push_back({std::move(name), std::move(fn)});
    return 0;
}

#define TEST(name) \
    static void test_##name(); \
    static int _reg_##name = register_test(#name, test_##name); \
    static void test_##name()

#define ASSERT(cond) \
    do { if (!(cond)) { \
        std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #cond "\n"; \
        throw std::runtime_error("assertion failed"); \
    } } while (0)

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { \
        std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " \
                  << (a) << " != " << (b) << "\n"; \
        throw std::runtime_error("assertion failed"); \
    } } while (0)

int main() {
    int passed = 0, failed = 0;
    for (auto& t : registry()) {
        try {
            t.fn();
            std::cout << "PASS: " << t.name << "\n";
            ++passed;
        } catch (std::exception& e) {
            std::cerr << "FAIL: " << t.name << " — " << e.what() << "\n";
            ++failed;
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
