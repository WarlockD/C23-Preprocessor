// test_string_pool.cpp — unit tests for StringPool.
#include <c23pp/string_pool.hpp>

#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Pull in test helpers from test_main.cpp
int register_test(std::string, std::function<void()>);

#define ASSERT(cond) \
    do { if (!(cond)) { \
        std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #cond "\n"; \
        throw std::runtime_error("assertion failed"); \
    } } while (0)
#define TEST(name) \
    static void test_##name(); \
    static int _reg_##name = register_test(#name, test_##name); \
    static void test_##name()

TEST(pool_intern_same_string) {
    c23pp::StringPool pool;
    auto a = pool.intern("hello");
    auto b = pool.intern("hello");
    // Same pointer (stable interned storage)
    ASSERT(a.data() == b.data());
}

TEST(pool_intern_different_strings) {
    c23pp::StringPool pool;
    auto a = pool.intern("foo");
    auto b = pool.intern("bar");
    ASSERT(a != b);
    ASSERT(pool.size() == 2);
}

TEST(pool_intern_empty_string) {
    c23pp::StringPool pool;
    auto sv = pool.intern("");
    ASSERT(sv.empty());
}

TEST(pool_thread_safe) {
    c23pp::StringPool pool;
    constexpr int N = 100;
    std::vector<std::thread> threads;
    std::vector<std::string_view> results(N);
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&, i] {
            results[i] = pool.intern("shared_key");
        });
    }
    for (auto& t : threads) t.join();
    // All results must point to the same storage.
    for (int i = 0; i < N; ++i)
        ASSERT(results[i].data() == results[0].data());
}
