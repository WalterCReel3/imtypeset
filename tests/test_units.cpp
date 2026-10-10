// src/units.h against tests/golden/core/units.golden, and the claims of the
// reference's tests/test_units.py that the golden does not already hold.

#include "core_golden.h"

#include "units.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

int g_violations = 0;

void count_violation(const char*)
{
    ++g_violations;
}

// Installs the counting contract handler for a scope.
struct ViolationScope {
    ViolationScope()
        : _before(contract_handler())
    {
        contract_handler() = &count_violation;
        g_violations = 0;
    }
    ~ViolationScope() { contract_handler() = _before; }
    ViolationScope(const ViolationScope&) = delete;
    ViolationScope& operator=(const ViolationScope&) = delete;

private:
    ContractHandler _before;
};

// The sign the port's clamp must take for a call the reference could not
// answer in 64 bits: every conversion is a product of its arguments by
// positive constants.
int sign_of_product(const std::vector<double>& arguments)
{
    int sign = 1;
    for (const double v : arguments) {
        if (std::signbit(v))
            sign = -sign;
    }
    return sign;
}

struct Call {
    bool known = false;
    bool integer_result = false;
    Scaled integer = 0;
    double real = 0.0;
    std::vector<double> arguments;
};

// Runs the conversion named `function` on the fields of `record` from `first`.
Call call(std::string_view function, const golden::Record& record, size_t first)
{
    Call out;
    out.known = true;
    if (function == "sp_from_pt") {
        out.arguments = {record.f64(first)};
        out.integer_result = true;
        out.integer = sp_from_pt(out.arguments[0]);
    } else if (function == "sp_from_bp") {
        out.arguments = {record.f64(first)};
        out.integer_result = true;
        out.integer = sp_from_bp(out.arguments[0]);
    } else if (function == "sp_from_px") {
        out.arguments = {record.f64(first), record.f64(first + 1)};
        out.integer_result = true;
        out.integer = sp_from_px(out.arguments[0], out.arguments[1]);
    } else if (function == "pt_from_sp") {
        const Scaled length = record.i64(first);
        out.arguments = {static_cast<double>(length)};
        out.real = pt_from_sp(length);
    } else if (function == "px_from_sp") {
        const Scaled length = record.i64(first);
        out.arguments = {static_cast<double>(length), record.f64(first + 1)};
        out.real = px_from_sp(length, out.arguments[1]);
    } else if (function == "round_to_px") {
        const Scaled length = record.i64(first);
        out.arguments = {static_cast<double>(length), record.f64(first + 1)};
        out.integer_result = true;
        out.integer = round_to_px(length, out.arguments[1]);
    } else {
        out.known = false;
    }
    return out;
}

void check_constant(const golden::Record& record)
{
    const std::string name = std::string(record.raw(0));
    const bool is_float = record.raw(1) == "float";
    if (name == "UNITY")
        CHECK_EQ(record.i64(2), UNITY);
    else if (name == "MAX_DIMEN")
        CHECK_EQ(record.i64(2), MAX_DIMEN);
    else if (name == "RUNNING")
        CHECK_EQ(record.i64(2), RUNNING);
    else if (name == "PT_PER_INCH")
        CHECK_EQ(record.f64(2), PT_PER_INCH);
    else if (name == "BP_PER_INCH")
        CHECK_EQ(record.f64(2), BP_PER_INCH);
    else if (name == "DEFAULT_PX_PER_INCH")
        CHECK_EQ(record.f64(2), DEFAULT_PX_PER_INCH);
    else if (name == "GlueOrder.NORMAL")
        CHECK_EQ(record.i64(2), static_cast<int64_t>(GlueOrder::NORMAL));
    else if (name == "GlueOrder.FIL")
        CHECK_EQ(record.i64(2), static_cast<int64_t>(GlueOrder::FIL));
    else if (name == "GlueOrder.FILL")
        CHECK_EQ(record.i64(2), static_cast<int64_t>(GlueOrder::FILL));
    else if (name == "GlueOrder.FILLL")
        CHECK_EQ(record.i64(2), static_cast<int64_t>(GlueOrder::FILLL));
    else
        FAIL("units.golden names a constant this test does not know: %s", name.c_str());
    CHECK(is_float == (name.find("PER_INCH") != std::string::npos));
}

} // namespace

TEST(conversions_match_the_reference)
{
    golden::File file;
    REQUIRE(file.load(golden::path("core/units.golden")));
    const std::vector<core_golden::Case> cases = core_golden::cases(file, "core/units");
    REQUIRE(!cases.empty());

    int constants = 0;
    int replayed = 0;
    int unanswerable = 0;
    for (const core_golden::Case& c : cases) {
        CHECK_CONTEXT("case %s", c.name.c_str());
        REQUIRE_EQ(c.kind, std::string("table"));
        core_golden::Reader reader(file, c);
        while (!reader.done()) {
            const golden::Record& record = reader.take();
            CHECK_CONTEXT("%s:%d", record.file, record.line);

            if (record.is("const")) {
                check_constant(record);
                ++constants;
                continue;
            }

            // What Python could not answer in 64 bits, the port answers by its
            // stated rule: 0 for a NaN, a clamp of the right sign otherwise.
            if (record.is("raises") || record.is("beyond_int64")) {
                const Call got = call(record.raw(0), record, 2);
                REQUIRE(got.known);
                REQUIRE(got.integer_result);
                Scaled expected = 0;
                if (record.is("beyond_int64")) {
                    expected = record.raw(1).substr(0, 1) == "-" ? -SCALED_LIMIT : SCALED_LIMIT;
                } else if (record.raw(1) == "valueerror") {
                    expected = 0;
                } else {
                    CHECK(record.raw(1) == "overflowerror" || record.raw(1) == "zerodivisionerror");
                    expected = sign_of_product(got.arguments) < 0 ? -SCALED_LIMIT : SCALED_LIMIT;
                }
                CHECK_EQ(got.integer, expected);
                ++unanswerable;
                continue;
            }

            const Call got = call(record.tag, record, 0);
            if (!got.known) {
                FAIL("units.golden holds a record this test does not replay: %.*s",
                     static_cast<int>(record.tag.size()), record.tag.data());
                continue;
            }
            const size_t result_field = record.size() - 1;
            if (got.integer_result) {
                CHECK_EQ(got.integer, record.i64(result_field));
            } else {
                // As text, so that a NaN compares equal to a NaN and the sign
                // of a zero is part of the answer.
                CHECK_EQ(core_golden::hex_float(got.real), std::string(record.raw(result_field)));
            }
            ++replayed;
        }
    }
    CHECK_EQ(constants, 10);
    // A golden that lost its rows must not read as a pass.
    CHECK(replayed > 2500);
    CHECK(unanswerable > 30);
}

TEST(py_round_ties_to_even_where_lround_would_not)
{
    CHECK_EQ(py_round(0.5), 0.0);
    CHECK_EQ(py_round(1.5), 2.0);
    CHECK_EQ(py_round(2.5), 2.0);
    CHECK_EQ(py_round(3.5), 4.0);
    CHECK_EQ(py_round(-0.5), -0.0);
    CHECK_EQ(py_round(-1.5), -2.0);
    CHECK_EQ(py_round(-2.5), -2.0);
    CHECK_EQ(py_round(0.49999999999999994), 0.0);
    CHECK_EQ(py_round(2.4999999999999996), 2.0);
    CHECK_EQ(py_round(2.5000000000000004), 3.0);
    CHECK_EQ(round_to_scaled(2.5), Scaled(2));
    CHECK_EQ(round_to_scaled(-3.5), Scaled(-4));
    // Beyond 2^52 every double is an integer already.
    CHECK_EQ(round_to_scaled(4503599627370497.0), Scaled(4503599627370497));
}

TEST(round_to_scaled_clamps_what_python_would_raise_for)
{
    const double inf = std::numeric_limits<double>::infinity();
    CHECK_EQ(round_to_scaled(std::numeric_limits<double>::quiet_NaN()), Scaled(0));
    CHECK_EQ(round_to_scaled(inf), SCALED_LIMIT);
    CHECK_EQ(round_to_scaled(-inf), -SCALED_LIMIT);
    CHECK_EQ(round_to_scaled(1e300), SCALED_LIMIT);
    CHECK_EQ(round_to_scaled(-1e300), -SCALED_LIMIT);
    CHECK_EQ(round_to_scaled(4611686018427387904.0), SCALED_LIMIT);
    // The largest double below 2^62 is still converted and not clamped.
    CHECK_EQ(round_to_scaled(4611686018427387392.0), Scaled(4611686018427387392));
    CHECK_EQ(round_to_scaled(-4611686018427387392.0), Scaled(-4611686018427387392));
    // Two clamped values add without overflowing.
    CHECK_EQ(SCALED_LIMIT + (SCALED_LIMIT - 1), std::numeric_limits<int64_t>::max());
}

TEST(floor_div_and_floor_mod_are_pythons_operators)
{
    for (int64_t a = -25; a <= 25; ++a) {
        for (int64_t b = -7; b <= 7; ++b) {
            if (b == 0)
                continue;
            CHECK_CONTEXT("a=%lld b=%lld", static_cast<long long>(a), static_cast<long long>(b));
            const double exact = static_cast<double>(a) / static_cast<double>(b);
            CHECK_EQ(floor_div(a, b), static_cast<int64_t>(std::floor(exact)));
            CHECK_EQ(ceil_div(a, b), static_cast<int64_t>(std::ceil(exact)));
            const int64_t remainder = floor_mod(a, b);
            CHECK_EQ(floor_div(a, b) * b + remainder, a);
            // The remainder takes the divisor's sign and is smaller than it.
            CHECK(remainder == 0 || ((remainder < 0) == (b < 0)));
            CHECK(std::llabs(remainder) < std::llabs(b));
            // The reference's spelling of a ceiling division.
            CHECK_EQ(ceil_div(a, b), -floor_div(-a, b));
        }
    }
    CHECK_EQ(floor_div(-1, 2), int64_t(-1));
    CHECK_EQ(floor_div(1, -2), int64_t(-1));
    CHECK_EQ(-7 / 2, -3); // what C++ does on its own, and why the helper exists
    CHECK_EQ(floor_div(-7, 2), int64_t(-4));
    CHECK_EQ(floor_mod(-7, 2), int64_t(1));
    CHECK_EQ(floor_mod(7, -2), int64_t(-1));

    const int64_t lowest = std::numeric_limits<int64_t>::min();
    const int64_t highest = std::numeric_limits<int64_t>::max();
    CHECK_EQ(floor_div(lowest, -1), highest);
    CHECK_EQ(ceil_div(lowest, -1), highest);
    CHECK_EQ(floor_mod(lowest, -1), int64_t(0));
    CHECK_EQ(floor_div(highest, -1), -highest);
    CHECK_EQ(floor_div(lowest, 2), lowest / 2);
    CHECK_EQ(floor_div(highest, 2), highest / 2);
}

TEST(division_by_zero_is_a_contract_violation_with_a_defined_answer)
{
    const ViolationScope scope;
    CHECK_EQ(floor_div(5, 0), int64_t(0));
    CHECK_EQ(ceil_div(5, 0), int64_t(0));
    CHECK_EQ(floor_mod(5, 0), int64_t(0));
    CHECK_EQ(g_violations, 3);
}

TEST(clamp_dimen_keeps_the_sentinel_and_bounds_everything_else)
{
    CHECK_EQ(clamp_dimen(0), Scaled(0));
    CHECK_EQ(clamp_dimen(MAX_DIMEN), MAX_DIMEN);
    CHECK_EQ(clamp_dimen(MAX_DIMEN + 1), MAX_DIMEN);
    CHECK_EQ(clamp_dimen(-MAX_DIMEN), -MAX_DIMEN);
    CHECK_EQ(clamp_dimen(RUNNING), RUNNING);
    CHECK_EQ(clamp_dimen(RUNNING - 1), -MAX_DIMEN);
    CHECK_EQ(clamp_dimen(SCALED_LIMIT), MAX_DIMEN);
    CHECK_EQ(clamp_dimen(-SCALED_LIMIT), -MAX_DIMEN);
    CHECK(RUNNING < -MAX_DIMEN);
}

// --- tests/test_units.py, where the golden does not already say it -------------------

TEST(a_point_and_a_big_point_are_different_lengths)
{
    CHECK_EQ(sp_from_pt(1.0), UNITY);
    CHECK_EQ(pt_from_sp(UNITY), 1.0);
    const Scaled one_inch = sp_from_pt(PT_PER_INCH);
    CHECK_EQ(sp_from_bp(BP_PER_INCH), one_inch);
    CHECK_NE(sp_from_pt(72.0), sp_from_bp(72.0));
    CHECK_EQ(sp_from_bp(72.0) - sp_from_pt(72.0), Scaled(17695));
    CHECK_NEAR(pt_from_sp(sp_from_bp(1.0)), 1.00375, 1e-5);
}

TEST(whole_and_dyadic_points_round_trip_exactly)
{
    for (const double points :
         {0.0, 1.0, 10.0, 12.0, -5.0, 16383.0, 0.5, 0.25, 12.75, -3.125, 1.0 / 65536.0}) {
        CHECK_CONTEXT("points=%g", points);
        CHECK_EQ(pt_from_sp(sp_from_pt(points)), points);
    }
    for (const double points : {0.1, 1.0 / 3.0, 9.9999, 12.045, 72.27, -0.7}) {
        CHECK_CONTEXT("points=%g", points);
        CHECK_NEAR(pt_from_sp(sp_from_pt(points)), points, 0.5 / static_cast<double>(UNITY));
    }
}

TEST(a_font_size_of_sixteen_is_sixteen_device_pixels)
{
    const Scaled sixteen_px = sp_from_px(16.0);
    CHECK_NEAR(pt_from_sp(sixteen_px), 12.045, 1e-3);
    CHECK_NEAR(px_from_sp(sixteen_px), 16.0, 1e-4);
    const Scaled ten_bp = sp_from_bp(10.0);
    CHECK_NEAR(px_from_sp(ten_bp, 72.0), 10.0, 1e-4);
    CHECK_NEAR(px_from_sp(ten_bp, 96.0), 10.0 * 96.0 / 72.0, 1e-4);
    for (const double px_per_inch : {96.0, 72.0}) {
        for (const double pixels : {0.0, 1.0, 16.0, 800.0, -12.5}) {
            CHECK_CONTEXT("pixels=%g at %g", pixels, px_per_inch);
            CHECK_NEAR(px_from_sp(sp_from_px(pixels, px_per_inch), px_per_inch), pixels, 1e-4);
        }
    }
}

TEST(rounding_the_sum_of_widths_beats_rounding_each_width)
{
    const Scaled item = sp_from_px(3.4);
    CHECK_EQ(round_to_px(3 * item), int64_t(10));
    CHECK_EQ(3 * round_to_px(item), int64_t(9));

    Scaled running = 0;
    double worst_positional = 0.0;
    for (int i = 0; i < 40; ++i) {
        running += item;
        const double error =
            std::fabs(static_cast<double>(round_to_px(running)) - px_from_sp(running));
        if (error > worst_positional)
            worst_positional = error;
    }
    CHECK(worst_positional <= 0.5);
    CHECK_NEAR(px_from_sp(running) - 40.0 * static_cast<double>(round_to_px(item)), 16.0, 0.05);
}

TEST(pixel_rounding_is_half_to_even_and_not_the_glue_rounding)
{
    // At 72.27 px/inch a pixel is a point, so these land on exact halves.
    CHECK_EQ(px_from_sp(sp_from_pt(2.5), PT_PER_INCH), 2.5);
    CHECK_EQ(round_to_px(sp_from_pt(2.5), PT_PER_INCH), int64_t(2));
    CHECK_EQ(round_to_px(sp_from_pt(3.5), PT_PER_INCH), int64_t(4));
    CHECK_EQ(round_to_px(sp_from_pt(-2.5), PT_PER_INCH), int64_t(-2));
}
