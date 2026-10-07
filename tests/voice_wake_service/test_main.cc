#include "test_framework.h"
#include <algorithm>
#include <string>
int main(int argc, char** argv) {
    if (argc == 1) return rodakos_test::RunAllTests();
    auto& tests = rodakos_test::TestCases();
    std::erase_if(tests, [&](const auto& test) { return test.name != std::string(argv[1]); });
    return tests.empty() ? 2 : rodakos_test::RunAllTests();
}
