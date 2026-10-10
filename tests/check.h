// The test harness: a registry of TEST(name) bodies, a handful of CHECK macros,
// and main(). One translation unit per test executable includes this, directly
// or through a helper header; ctest reads the exit status.
//
//     #include "check.h"
//
//     TEST(badness_of_zero_stretch)
//     {
//         CHECK_EQ(badness(1, 0), 10000);
//     }
//
// Built with -fno-exceptions, so a failed CHECK records and carries on. Where
// carrying on would dereference something that is not there, use REQUIRE, which
// returns from the enclosing function; it therefore only works in a function
// returning void, which a TEST body is.
//
// Command line of every test executable:
//     --list            print the test names and exit
//     --filter=TEXT     run only tests whose name contains TEXT (repeatable: any)
//     -v, --verbose     print each test's name as it starts
//
// Define IMTYPESET_CHECK_NO_MAIN before including to supply your own main() and
// call check::run(argc, argv) from it.

#pragma once

#include <cinttypes>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace check
{

using TestFn = void (*)();

struct TestCase {
    const char* name;
    const char* file;
    int line;
    TestFn fn;
};

struct State {
    std::vector<TestCase> tests;
    // Lines pushed by CHECK_CONTEXT, innermost last; printed with a failure.
    std::vector<std::string> context;
    const char* current_test = nullptr;
    long checks = 0;
    long failures = 0;
    long failures_in_test = 0;
    // Set while an ExpectFailures is alive: failures are counted, not printed.
    bool muted = false;
};

inline State& state()
{
    static State s;
    return s;
}

// A golden replay that goes wrong tends to go wrong on every record. The count
// stays exact; only the printing stops.
constexpr long max_failures_printed_per_test = 25;

#if defined(__GNUC__) || defined(__clang__)
#define IMTYPESET_CHECK_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define IMTYPESET_CHECK_PRINTF(fmt, args)
#endif

inline std::string vformat(const char* fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (n <= 0)
        return std::string();
    std::string out(static_cast<size_t>(n), '\0');
    std::vsnprintf(out.data(), static_cast<size_t>(n) + 1, fmt, args);
    return out;
}

inline std::string format(const char* fmt, ...) IMTYPESET_CHECK_PRINTF(1, 2);
inline std::string format(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::string out = vformat(fmt, args);
    va_end(args);
    return out;
}

// Counts one failure and prints it with the active context, unless this test
// has already printed its share.
inline void fail(const char* file, int line, const std::string& message)
{
    State& s = state();
    ++s.failures;
    ++s.failures_in_test;
    if (s.muted || s.failures_in_test > max_failures_printed_per_test)
        return;
    std::fprintf(stderr, "%s:%d: [%s] %s\n", file, line, s.current_test ? s.current_test : "?",
                 message.c_str());
    for (const std::string& c : s.context)
        std::fprintf(stderr, "    in: %s\n", c.c_str());
    if (s.failures_in_test == max_failures_printed_per_test)
        std::fprintf(stderr, "    (further failures in this test are counted, not printed)\n");
}

inline void count_check()
{
    ++state().checks;
}

struct Registrar {
    Registrar(const char* name, const char* file, int line, TestFn fn)
    {
        state().tests.push_back(TestCase{name, file, line, fn});
    }
};

// Scoped: pushes a line of context on construction, pops it on destruction.
struct ContextScope {
    explicit ContextScope(std::string text) { state().context.push_back(std::move(text)); }
    ~ContextScope() { state().context.pop_back(); }
    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;
};

// For testing code that reports through fail(): while one of these is alive,
// failures are counted but not printed, count() says how many there have been
// since it was made, and on destruction the run's totals are put back as they
// were, so the failures it observed do not fail the test.
class ExpectFailures
{
public:
    ExpectFailures()
        : _failures(state().failures)
        , _failures_in_test(state().failures_in_test)
        , _muted(state().muted)
    {
        state().muted = true;
    }
    ~ExpectFailures()
    {
        state().failures = _failures;
        state().failures_in_test = _failures_in_test;
        state().muted = _muted;
    }
    ExpectFailures(const ExpectFailures&) = delete;
    ExpectFailures& operator=(const ExpectFailures&) = delete;

    long count() const { return state().failures - _failures; }

private:
    long _failures;
    long _failures_in_test;
    bool _muted;
};

// --- Printing a value ---------------------------------------------------------
// No iostreams, so the printable types are the ones listed here. Anything else
// prints as its size, and the failure line still carries both expressions.
// Floating point prints as %.17g and as a hex float: the first to read, the
// second because goldens are compared exactly and decimal hides the last bit.

inline std::string quoted(std::string_view s)
{
    std::string out = "\"";
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += ch;
        } else if (c < 0x20 || c == 0x7f) {
            out += format("\\x%02x", static_cast<unsigned>(c));
        } else {
            out += ch;
        }
    }
    out += '"';
    return out;
}

template<typename T>
std::string to_text(const T& v)
{
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, bool>) {
        return v ? "true" : "false";
    } else if constexpr (std::is_same_v<D, char>) {
        return format("'%c' (%d)", v, static_cast<int>(v));
    } else if constexpr (std::is_enum_v<D>) {
        return format("%lld", static_cast<long long>(static_cast<std::underlying_type_t<D>>(v)));
    } else if constexpr (std::is_integral_v<D> && std::is_signed_v<D>) {
        return format("%lld", static_cast<long long>(v));
    } else if constexpr (std::is_integral_v<D>) {
        return format("%llu", static_cast<unsigned long long>(v));
    } else if constexpr (std::is_floating_point_v<D>) {
        return format("%.17g (%a)", static_cast<double>(v), static_cast<double>(v));
    } else if constexpr (std::is_array_v<T> && std::is_convertible_v<const T&, std::string_view>) {
        return quoted(std::string_view(v)); // a char array, which is never null
    } else if constexpr (std::is_same_v<D, const char*> || std::is_same_v<D, char*>) {
        return v ? quoted(v) : std::string("(null)");
    } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        return quoted(std::string_view(v));
    } else if constexpr (std::is_pointer_v<D>) {
        return format("%p", static_cast<const void*>(v));
    } else if constexpr (std::is_null_pointer_v<D>) {
        return "nullptr";
    } else {
        return format("<%zu-byte value>", sizeof(T));
    }
}

// --- Comparing two values --------------------------------------------------------
// Integers compare by mathematical value whatever their signedness, so
// CHECK_EQ(vec.size(), 3) neither warns under -Wsign-compare nor converts -1
// into a large unsigned number. C strings compare by content.

template<typename T>
constexpr bool string_like =
    std::is_convertible_v<const T&, std::string_view> && !std::is_null_pointer_v<T>;

// What a string-like value holds: its bytes, or nothing at all if it is a null
// char pointer.
struct Text {
    bool null;
    std::string_view view;
};

template<typename T>
Text as_text(const T& v)
{
    if constexpr (std::is_pointer_v<T>) {
        return v == nullptr ? Text{true, std::string_view()} : Text{false, std::string_view(v)};
    } else {
        return Text{false, std::string_view(v)}; // an array or a string type
    }
}

template<typename A, typename B>
bool equal(const A& a, const B& b)
{
    using DA = std::decay_t<A>;
    using DB = std::decay_t<B>;
    constexpr bool int_a = std::is_integral_v<DA> && !std::is_same_v<DA, bool>;
    constexpr bool int_b = std::is_integral_v<DB> && !std::is_same_v<DB, bool>;
    if constexpr (int_a && int_b) {
        if constexpr (std::is_signed_v<DA> == std::is_signed_v<DB>) {
            return a == b;
        } else if constexpr (std::is_signed_v<DA>) {
            return a >= 0 && static_cast<std::make_unsigned_t<DA>>(a) == b;
        } else {
            return b >= 0 && a == static_cast<std::make_unsigned_t<DB>>(b);
        }
    } else if constexpr (string_like<A> && string_like<B>) {
        // By content. A null char pointer equals only another null.
        const Text ta = as_text(a);
        const Text tb = as_text(b);
        return ta.null == tb.null && ta.view == tb.view;
    } else if constexpr (string_like<A> && std::is_null_pointer_v<DB>) {
        return as_text(a).null;
    } else if constexpr (std::is_null_pointer_v<DA> && string_like<B>) {
        return as_text(b).null;
    } else {
        return a == b;
    }
}

template<typename A, typename B>
bool check_eq(const char* file, int line, const char* a_text, const char* b_text, const A& a,
              const B& b)
{
    count_check();
    if (equal(a, b))
        return true;
    fail(file, line,
         format("CHECK_EQ(%s, %s)\n    left:  %s\n    right: %s", a_text, b_text,
                to_text(a).c_str(), to_text(b).c_str()));
    return false;
}

template<typename A, typename B>
bool check_ne(const char* file, int line, const char* a_text, const char* b_text, const A& a,
              const B& b)
{
    count_check();
    if (!equal(a, b))
        return true;
    fail(file, line, format("CHECK_NE(%s, %s)\n    both:  %s", a_text, b_text, to_text(a).c_str()));
    return false;
}

inline bool check_true(const char* file, int line, const char* text, bool ok)
{
    count_check();
    if (!ok)
        fail(file, line, format("CHECK(%s)", text));
    return ok;
}

// |a - b| <= tolerance. A NaN on either side fails, as does a NaN tolerance.
inline bool check_near(const char* file, int line, const char* a_text, const char* b_text, double a,
                       double b, double tolerance)
{
    count_check();
    if (std::fabs(a - b) <= tolerance)
        return true;
    fail(file, line,
         format("CHECK_NEAR(%s, %s, %g)\n    left:  %s\n    right: %s\n    diff:  %g", a_text,
                b_text, tolerance, to_text(a).c_str(), to_text(b).c_str(), a - b));
    return false;
}

// --- Running ----------------------------------------------------------------------

inline bool selected(const char* name, const std::vector<std::string>& filters)
{
    if (filters.empty())
        return true;
    for (const std::string& f : filters)
        if (std::strstr(name, f.c_str()) != nullptr)
            return true;
    return false;
}

// Returns the process exit status: 0 when every selected test passed, 1 when a
// check failed, 2 for a bad command line or a filter that selected nothing (a
// misspelt filter must not read as a pass).
inline int run(int argc, char** argv)
{
    State& s = state();
    std::vector<std::string> filters;
    bool list = false;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--list") == 0) {
            list = true;
        } else if (std::strcmp(arg, "-v") == 0 || std::strcmp(arg, "--verbose") == 0) {
            verbose = true;
        } else if (std::strncmp(arg, "--filter=", 9) == 0) {
            filters.emplace_back(arg + 9);
        } else if (std::strcmp(arg, "--filter") == 0 && i + 1 < argc) {
            filters.emplace_back(argv[++i]);
        } else {
            std::fprintf(stderr, "usage: %s [--list] [--filter=TEXT]... [-v]\n", argv[0]);
            return 2;
        }
    }

    if (list) {
        for (const TestCase& t : s.tests)
            if (selected(t.name, filters))
                std::printf("%s\n", t.name);
        return 0;
    }

    long ran = 0;
    long failed_tests = 0;
    for (const TestCase& t : s.tests) {
        if (!selected(t.name, filters))
            continue;
        ++ran;
        s.current_test = t.name;
        s.failures_in_test = 0;
        if (verbose) {
            std::printf("[ run ] %s\n", t.name);
            std::fflush(stdout);
        }
        t.fn();
        if (s.failures_in_test != 0) {
            ++failed_tests;
            std::fprintf(stderr, "[FAIL ] %s (%s:%d): %ld failed check(s)\n", t.name, t.file,
                         t.line, s.failures_in_test);
        }
    }
    s.current_test = nullptr;

    if (ran == 0) {
        std::fprintf(stderr, "%s: no test selected (%zu registered)\n", argv[0], s.tests.size());
        return 2;
    }
    std::printf("%s: %ld test(s), %ld check(s), %ld failed check(s) in %ld test(s)\n",
                failed_tests == 0 ? "ok" : "FAILED", ran, s.checks, s.failures, failed_tests);
    return failed_tests == 0 ? 0 : 1;
}

} // namespace check

#define IMTYPESET_CHECK_CAT2(a, b) a##b
#define IMTYPESET_CHECK_CAT(a, b)  IMTYPESET_CHECK_CAT2(a, b)

// Defines and registers a test. The name is an identifier, not a string.
#define TEST(name)                                                               \
    static void IMTYPESET_CHECK_CAT(check_test_, name)();                        \
    static const ::check::Registrar IMTYPESET_CHECK_CAT(check_registrar_, name)( \
        #name, __FILE__, __LINE__, &IMTYPESET_CHECK_CAT(check_test_, name));     \
    static void IMTYPESET_CHECK_CAT(check_test_, name)()

// Each CHECK is an expression yielding whether it held, so a caller can branch.
#define CHECK(cond)    ::check::check_true(__FILE__, __LINE__, #cond, static_cast<bool>(cond))
#define CHECK_EQ(a, b) ::check::check_eq(__FILE__, __LINE__, #a, #b, (a), (b))
#define CHECK_NE(a, b) ::check::check_ne(__FILE__, __LINE__, #a, #b, (a), (b))
#define CHECK_NEAR(a, b, tolerance)                                         \
    ::check::check_near(__FILE__, __LINE__, #a, #b, static_cast<double>(a), \
                        static_cast<double>(b), static_cast<double>(tolerance))

// As CHECK and CHECK_EQ, then return from the enclosing void function on failure.
#define REQUIRE(cond)     \
    do {                  \
        if (!CHECK(cond)) \
            return;       \
    } while (0)
#define REQUIRE_EQ(a, b)     \
    do {                     \
        if (!CHECK_EQ(a, b)) \
            return;          \
    } while (0)

// An unconditional failure with a printf-style message.
#define FAIL(...) ::check::fail(__FILE__, __LINE__, ::check::format(__VA_ARGS__))

// Adds a printf-style line to every failure reported until the end of the
// enclosing scope: which golden file, which case, which record.
#define CHECK_CONTEXT(...)                                          \
    const ::check::ContextScope IMTYPESET_CHECK_CAT(check_context_, \
                                                    __LINE__)(::check::format(__VA_ARGS__))

#ifndef IMTYPESET_CHECK_NO_MAIN
int main(int argc, char** argv)
{
    return ::check::run(argc, argv);
}
#endif
