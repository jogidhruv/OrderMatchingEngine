#pragma once
// ---------------------------------------------------------------------------
// Minimal zero-dependency unit-test harness.
// ---------------------------------------------------------------------------
// Tests register themselves at static-init time via the TEST(name) macro and are
// run by run_all(). Assertions (CHECK / CHECK_EQ / CHECK_NE) record a failure and
// continue, so one test can report several problems in a single run. run_all()
// returns the number of failed tests, making it directly usable as a process exit
// code (0 == success) for ctest / CI.

#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace qtest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

// Global registry of tests. Defined in test_framework.cpp.
std::vector<TestCase>& registry();

// Registers one test; returns a dummy int so it can seed a static initialiser.
int register_test(const std::string& name, std::function<void()> fn);

// Records a failed assertion against the currently-running test.
void report_failure(const std::string& expr, const char* file, int line,
                    const std::string& detail);

// Runs every registered test; returns the count of tests that failed.
int run_all();

// Stringifies a value for failure messages. Enums are printed as their
// underlying integer so OrderStatus / RejectReason etc. work without overloads.
template <class T>
std::string to_str(const T& v) {
    std::ostringstream os;
    if constexpr (std::is_enum_v<T>) {
        os << static_cast<long long>(static_cast<std::underlying_type_t<T>>(v));
    } else {
        os << v;
    }
    return os.str();
}

}  // namespace qtest

// Defines and self-registers a test function.
#define TEST(name)                                                            \
    static void name();                                                       \
    static const int _qtest_reg_##name =                                      \
        ::qtest::register_test(#name, name);                                  \
    static void name()

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            ::qtest::report_failure(#cond, __FILE__, __LINE__, "");           \
        }                                                                     \
    } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        auto _qa = (a);                                                       \
        auto _qb = (b);                                                       \
        if (!((_qa) == (_qb))) {                                              \
            ::qtest::report_failure(#a " == " #b, __FILE__, __LINE__,         \
                                    "got " + ::qtest::to_str(_qa) + " vs " +  \
                                        ::qtest::to_str(_qb));                \
        }                                                                     \
    } while (0)

#define CHECK_NE(a, b)                                                        \
    do {                                                                      \
        auto _qa = (a);                                                       \
        auto _qb = (b);                                                       \
        if ((_qa) == (_qb)) {                                                 \
            ::qtest::report_failure(#a " != " #b, __FILE__, __LINE__,         \
                                    "both " + ::qtest::to_str(_qa));          \
        }                                                                     \
    } while (0)
