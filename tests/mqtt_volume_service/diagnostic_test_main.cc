#include "test_framework.h"

#include <cstring>

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--filter") == 0) {
        auto& cases = rodakos_test::TestCases();
        std::vector<rodakos_test::TestCase> selected;
        for (const auto& test : cases)
            if (std::strstr(test.name, argv[2]) != nullptr) selected.push_back(test);
        if (selected.size() != 1) return 2;
        cases = std::move(selected);
    } else if (argc != 1) return 2;
    return rodakos_test::RunAllTests();
}
