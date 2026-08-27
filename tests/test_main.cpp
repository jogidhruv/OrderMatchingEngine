#include "test_framework.hpp"

// All test cases self-register in domain_tests.cpp; main just runs them and
// forwards the failed-test count as the process exit code.
int main() {
    return qtest::run_all();
}
