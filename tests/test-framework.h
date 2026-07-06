#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

struct TestCase { const char *name; std::function<void()> fn; };
inline std::vector<TestCase> &test_registry() { static std::vector<TestCase> r; return r; }
inline int &test_failures() { static int f = 0; return f; }
struct TestRegistrar {
    TestRegistrar(const char *n, std::function<void()> f) { test_registry().push_back({n, std::move(f)}); }
};
#define TEST(name) \
    static void name(); \
    static TestRegistrar reg_##name(#name, name); \
    static void name()
#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++test_failures(); } } while (0)
#define CHECK_NEAR(a, b, eps) \
    do { double va = (a), vb = (b); if (std::fabs(va - vb) > (eps)) { \
        std::printf("FAIL %s:%d: %s=%f vs %s=%f\n", __FILE__, __LINE__, #a, va, #b, vb); ++test_failures(); } } while (0)

int main()
{
    for (auto &t : test_registry()) t.fn();
    if (test_failures()) { std::printf("%d failure(s)\n", test_failures()); return 1; }
    std::printf("all %zu tests passed\n", test_registry().size());
    return 0;
}
