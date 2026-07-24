#pragma once

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace rodakos_home_ui_test {

using TestFunction = void (*)();

struct TestCase {
    const char* name;
    TestFunction function;
};

inline std::vector<TestCase>& TestCases() {
    static std::vector<TestCase> cases;
    return cases;
}

class Registrar {
public:
    Registrar(const char* name, TestFunction function) {
        TestCases().push_back({name, function});
    }
};

[[noreturn]] inline void Fail(const char* expression, const char* file, int line) {
    throw std::runtime_error(
        std::string(file) + ":" + std::to_string(line) +
        ": check failed: " + expression);
}

inline int RunAllTests() {
    int failures = 0;
    for (const auto& test : TestCases()) {
        try {
            test.function();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }
    std::cout << TestCases().size() << " tests, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}

}  // namespace rodakos_home_ui_test

#define RODAK_CONCAT_INNER(lhs, rhs) lhs##rhs
#define RODAK_CONCAT(lhs, rhs) RODAK_CONCAT_INNER(lhs, rhs)

#define RODAK_TEST(name) \
    static void RODAK_CONCAT(RodakTestFunction_, __LINE__)(); \
    static ::rodakos_home_ui_test::Registrar \
        RODAK_CONCAT(RodakTestRegistrar_, __LINE__)( \
            name, &RODAK_CONCAT(RodakTestFunction_, __LINE__)); \
    static void RODAK_CONCAT(RodakTestFunction_, __LINE__)()

#define RODAK_CHECK(expression) \
    do { \
        if (!(expression)) { \
            ::rodakos_home_ui_test::Fail(#expression, __FILE__, __LINE__); \
        } \
    } while (false)
#define RODAK_CHECK_FALSE(expression) RODAK_CHECK(!(expression))

#define RODAK_CHECK_EQ(actual_expression, expected_expression) \
    do { \
        const auto rodak_actual = (actual_expression); \
        const auto rodak_expected = (expected_expression); \
        if (!(rodak_actual == rodak_expected)) { \
            ::rodakos_home_ui_test::Fail( \
                #actual_expression " == " #expected_expression, __FILE__, __LINE__); \
        } \
    } while (false)

#define RODAK_CHECK_NE(actual_expression, expected_expression) \
    do { \
        const auto rodak_actual = (actual_expression); \
        const auto rodak_expected = (expected_expression); \
        if (!(rodak_actual != rodak_expected)) { \
            ::rodakos_home_ui_test::Fail( \
                #actual_expression " != " #expected_expression, __FILE__, __LINE__); \
        } \
    } while (false)
