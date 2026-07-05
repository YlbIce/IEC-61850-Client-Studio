#pragma once

// Lightweight unit-test framework for IEC 61850 Client Studio backend.
// Zero external dependencies — just standard C++17.

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
#include <functional>

namespace studio_test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
    bool failed = false;
    std::string failureMsg;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int registerTest(const char* name, std::function<void()> fn) {
    registry().push_back({name, std::move(fn), false, ""});
    return static_cast<int>(registry().size());
}

inline void fail(const char* file, int line, const std::string& msg) {
    throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + " - " + msg);
}

#define TEST(name)                                                              \
    static void test_##name();                                                  \
    static int reg_##name = studio_test::registerTest(#name, test_##name);      \
    static void test_##name()

#define EXPECT_EQ(a, b)                                                         \
    do {                                                                        \
        auto _va = (a);                                                         \
        auto _vb = (b);                                                         \
        if (!(_va == _vb)) {                                                    \
            studio_test::fail(__FILE__, __LINE__,                               \
                "EXPECT_EQ failed: " + std::to_string(_va) + " != " + std::to_string(_vb)); \
        }                                                                       \
    } while (0)

#define EXPECT_NE(a, b)                                                         \
    do {                                                                        \
        auto _va = (a);                                                         \
        auto _vb = (b);                                                         \
        if (_va == _vb) {                                                       \
            studio_test::fail(__FILE__, __LINE__,                               \
                "EXPECT_NE failed: both equal " + std::to_string(_va));        \
        }                                                                       \
    } while (0)

#define EXPECT_TRUE(x)                                                          \
    do {                                                                        \
        if (!(x)) {                                                             \
            studio_test::fail(__FILE__, __LINE__, "EXPECT_TRUE failed");        \
        }                                                                       \
    } while (0)

#define EXPECT_FALSE(x)                                                         \
    do {                                                                        \
        if (x) {                                                                \
            studio_test::fail(__FILE__, __LINE__, "EXPECT_FALSE failed");       \
        }                                                                       \
    } while (0)

#define EXPECT_STREQ(a, b)                                                      \
    do {                                                                        \
        std::string _sa = (a);                                                  \
        std::string _sb = (b);                                                  \
        if (_sa != _sb) {                                                       \
            studio_test::fail(__FILE__, __LINE__,                               \
                "EXPECT_STREQ failed: \"" + _sa + "\" != \"" + _sb + "\"");     \
        }                                                                       \
    } while (0)

#define EXPECT_STRNE(a, b)                                                      \
    do {                                                                        \
        std::string _sa = (a);                                                  \
        std::string _sb = (b);                                                  \
        if (_sa == _sb) {                                                       \
            studio_test::fail(__FILE__, __LINE__,                               \
                "EXPECT_STRNE failed: both are \"" + _sa + "\"");               \
        }                                                                       \
    } while (0)

#define EXPECT_CONTAINS(haystack, needle)                                       \
    do {                                                                        \
        std::string _hs = (haystack);                                           \
        std::string _nd = (needle);                                             \
        if (_hs.find(_nd) == std::string::npos) {                               \
            studio_test::fail(__FILE__, __LINE__,                               \
                "EXPECT_CONTAINS failed: \"" + _nd + "\" not in \"" + _hs + "\""); \
        }                                                                       \
    } while (0)

inline int runAll() {
    int passed = 0;
    int failed = 0;
    auto& cases = registry();

    std::printf("\n[==========] Running %zu test cases\n", cases.size());
    std::printf("[----------] Global test environment set-up\n\n");

    for (auto& tc : cases) {
        std::printf("[ RUN      ] %s\n", tc.name.c_str());
        try {
            tc.fn();
            std::printf("[       OK ] %s\n", tc.name.c_str());
            passed++;
        } catch (const std::exception& e) {
            tc.failed = true;
            tc.failureMsg = e.what();
            std::printf("[  FAILED  ] %s\n", tc.name.c_str());
            std::printf("    %s\n", e.what());
            failed++;
        } catch (...) {
            tc.failed = true;
            tc.failureMsg = "unknown exception";
            std::printf("[  FAILED  ] %s (unknown exception)\n", tc.name.c_str());
            failed++;
        }
    }

    std::printf("\n[----------] Global test environment tear-down\n");
    std::printf("[==========] %zu tests ran\n", cases.size());
    std::printf("[  PASSED  ] %d tests\n", passed);
    if (failed > 0) {
        std::printf("[  FAILED  ] %d tests\n", failed);
        std::printf("\n[  FAILED  LIST ]\n");
        for (const auto& tc : cases) {
            if (tc.failed) {
                std::printf("  - %s\n", tc.name.c_str());
            }
        }
    }
    std::printf("\n");
    return failed > 0 ? 1 : 0;
}

}  // namespace studio_test
