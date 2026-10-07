#include "test_framework.h"
#include <cstring>

int main(int argc, char** argv) {
    const char* filter = argc == 3 && std::strcmp(argv[1], "--filter") == 0 ? argv[2] : nullptr;
    if (argc != 1 && filter == nullptr) return 2;
    if (filter) {
        auto& tests = rodakos_test::TestCases();
        std::vector<rodakos_test::TestCase> selected;
        for (const auto& test : tests) {
            if (std::strstr(test.name, filter)) selected.push_back(test);
        }
        if (selected.empty()) return 3;
        tests = std::move(selected);
    }
    return rodakos_test::RunAllTests();
}
