#include "test_framework.h"

#include <algorithm>
#include <cstring>

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--filter") == 0) {
        auto& cases = rodakos_test::TestCases();
        cases.erase(std::remove_if(cases.begin(), cases.end(), [&](const auto& test) {
            return std::strstr(test.name, argv[2]) == nullptr;
        }), cases.end());
        if (cases.empty()) return 2;
    } else if (argc != 1) {
        return 2;
    }
    return rodakos_test::RunAllTests();
}
