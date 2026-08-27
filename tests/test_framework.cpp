#include "test_framework.hpp"

#include <iostream>

namespace qtest {

namespace {
// Failure counter for the test currently executing. Reset before each test.
int g_current_failures = 0;
}  // namespace

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

int register_test(const std::string& name, std::function<void()> fn) {
    registry().push_back({name, std::move(fn)});
    return 0;
}

void report_failure(const std::string& expr, const char* file, int line,
                    const std::string& detail) {
    ++g_current_failures;
    std::cerr << "    FAIL: " << expr << "  (" << file << ':' << line << ')';
    if (!detail.empty()) {
        std::cerr << "  [" << detail << ']';
    }
    std::cerr << '\n';
}

int run_all() {
    int passed = 0;
    int failed = 0;
    for (const TestCase& tc : registry()) {
        g_current_failures = 0;
        try {
            tc.fn();
        } catch (const std::exception& e) {
            report_failure(std::string("threw std::exception: ") + e.what(),
                           __FILE__, __LINE__, "");
        } catch (...) {
            report_failure("threw unknown exception", __FILE__, __LINE__, "");
        }
        if (g_current_failures == 0) {
            ++passed;
            std::cout << "[ PASS ] " << tc.name << '\n';
        } else {
            ++failed;
            std::cout << "[ FAIL ] " << tc.name << "  ("
                      << g_current_failures << " assertion(s))\n";
        }
    }
    std::cout << "\n==================================================\n"
              << "  " << passed << " passed, " << failed << " failed, "
              << registry().size() << " total\n"
              << "==================================================\n";
    return failed;
}

}  // namespace qtest
