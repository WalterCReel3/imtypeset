// Dimensions in TeX's scaled points, the conversions in and out of them, and
// the arithmetic helpers every other module uses to agree with the reference.
// Mirrors advtext/units.py.
//
// WHAT OF tex.web THIS IS. Part 7, "Arithmetic with scaled dimensions"
// (sections 99 to 109), as far as its vocabulary goes, and four constants
// defined further on:
//
//   section 101   scaled, unity                    Scaled, UNITY
//   section 138   null_flag                        RUNNING
//   section 150   glue_ord: normal, fil, fill,     GlueOrder
//                 filll
//   section 421   max_dimen                        MAX_DIMEN
//
// Section 100's half is in src/math_mlist.h, its only user; section 108's
// badness is src/badness. None of the part's other routines is here
// (round_decimals, print_scaled, mult_and_add, x_over_n, xn_over_d): they
// exist to keep 32-bit arithmetic from overflowing and to truncate toward
// zero, and this port follows the reference, whose integers do not overflow
// and whose divisions floor or round half to even (floor_div, ceil_div,
// py_round below). Each place where that changes an answer tex.web would give
// is listed in docs/divergences.md.
//
// tex.web has no pixels: the px, bp and resolution conversions at the end of
// this file are the reference's.
//
// A length is an integer number of scaled points (2^-16 pt). Lengths stay exact
// while a list is packed and are rounded once, at ship-out; the only floats in
// the layout core are a box's glue ratio and the values crossing this header's
// conversions.
//
// Scaled is 64 bits and its arithmetic is unchecked. The reference's integers
// are unbounded and TeX's are 32 bits with MAX_DIMEN as the largest legal
// length (section 104 sets arith_error when a result leaves it); this port
// keeps TeX's bound as the invariant and Python's headroom as the margin:
//
//   - A dimension stored in a node is within +-MAX_DIMEN (2^30 - 1), or is the
//     RUNNING sentinel. Whatever accepts dimensions from a host or a font
//     establishes that, with clamp_dimen() or by construction; the node
//     constructors do not check it.
//   - Under that invariant no sum formed by packing, breaking or shipping a
//     list can overflow: a list is indexed by 32 bits, and 2^32 terms of 2^30
//     stay below 2^63. Sums over fewer than 2^22 nodes also stay below 2^53,
//     which is what makes (double)a / (double)b the correctly rounded quotient
//     the reference's int / int is.
//   - Outside the invariant the results are not layout, but they are defined:
//     the float conversions clamp, floor_div/ceil_div define their two
//     overflowing cases, and every sum the core forms over a list somebody
//     else built goes through add_checked/sub_checked, which stop at
//     +-SCALED_MAX and say so. Packing and ship-out report that as an error.
//
// The conversions take a resolution argument because a point is 1/72.27 inch by
// definition and a pixel is whatever the display says.

#pragma once

// Every result here is IEEE double arithmetic with each operation rounded on
// its own, compared with the reference to the last bit. Under fast-math the
// compiler may reassociate, fuse and assume no infinities, and a line break or
// a pixel position then depends on the optimiser. This header is included by
// every source of the library, so the refusal is here.
#if (defined(__FAST_MATH__) || defined(_M_FP_FAST) ||            \
     (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__)) && \
    !defined(IMTYPESET_ALLOW_FAST_MATH)
#error \
    "imtypeset must not be compiled with fast-math (-ffast-math, -Ofast, -ffinite-math-only, /fp:fast): line breaks and pixel positions would differ. Compile its sources without it, or define IMTYPESET_ALLOW_FAST_MATH to accept unspecified results."
#endif

#include "imgui.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace ImTypeset::detail
{

// --- Contracts ------------------------------------------------------------------
//
// Where the reference raises for a caller's programming error, an internal
// entry point here checks with IMTYPESET_CONTRACT and then returns something
// defined: IM_ASSERT is commonly compiled out, so the code after it must not
// depend on it having fired.
//
//     if (!IMTYPESET_CONTRACT(t >= 0, "badness: t must not be negative"))
//         return INF_BAD;
//
// A violation calls the installed handler if there is one and IM_ASSERT
// otherwise. The handler exists so that a test can run the path that follows a
// violation in a build where assertions are live; it is process-wide, is not
// synchronised, and nothing outside tests installs one.

using ContractHandler = void (*)(const char* what);

inline ContractHandler& contract_handler()
{
    static ContractHandler handler = nullptr;
    return handler;
}

// Reports one violation. Always returns false, so IMTYPESET_CONTRACT can be the
// condition of the `if` that takes the defined way out.
inline bool contract_violated(const char* what)
{
    if (const ContractHandler handler = contract_handler())
        handler(what);
    else
        IM_ASSERT(false && "imtypeset: an internal contract was violated");
    (void)what;
    return false;
}

#define IMTYPESET_CONTRACT(condition, what) \
    ((condition) ? true : ::ImTypeset::detail::contract_violated(what))

// --- The type and its constants ---------------------------------------------------

// A length in scaled points.
using Scaled = int64_t;

// Scaled points in one printer's point: tex.web's unity (section 101).
constexpr Scaled UNITY = Scaled(1) << 16;

// Printer's points per inch: the definition of TeX's pt.
constexpr double PT_PER_INCH = 72.27;

// Big points per inch. bp is what PostScript, PDF and CSS call a point.
constexpr double BP_PER_INCH = 72.0;

// The display resolution assumed when none is given: the CSS reference pixel,
// and what ImGui's unscaled coordinates behave like at 100% scale.
constexpr double DEFAULT_PX_PER_INCH = 96.0;

// The largest legal dimension: tex.web's max_dimen (section 421). About
// 16384 pt.
constexpr Scaled MAX_DIMEN = (Scaled(1) << 30) - 1;

// The running-dimension sentinel of a rule: tex.web's null_flag (section 138),
// and the same number. Resolved against the enclosing box at ship-out. A
// sentinel and not a length. It is deliberately a large negative number and
// not a flag, because hpack relies on it losing a max() against a real height
// (section 653).
constexpr Scaled RUNNING = -(Scaled(1) << 30);

// What the float conversions clamp to. Far outside any dimension, exactly
// representable as a double, and small enough that two of them add without
// overflowing.
constexpr Scaled SCALED_LIMIT = Scaled(1) << 62;

// The four orders of infinity of a glue component: tex.web's glue_ord, with
// its values normal, fil, fill and filll (section 150). The numeric order is
// the semantics: the highest order with a nonzero total is used as if no other
// were present (section 646).
enum class GlueOrder : uint8_t { NORMAL = 0, FIL = 1, FILL = 2, FILLL = 3 };

// The ends of what a Scaled can hold when a sum is cut off. Symmetric, so that
// negating one of them is defined.
constexpr Scaled SCALED_MAX = std::numeric_limits<int64_t>::max();

// --- Arithmetic on dimensions nobody vouched for ---------------------------------
//
// Not tex.web's, which adds dimensions unchecked in most places and relies on
// max_dimen being honoured where they were scanned (decision D33).

// a + b, or the end of the range the true sum lies past, with `overflowed` set
// (it is never cleared). The same value as a + b whenever that is representable.
constexpr Scaled add_checked(Scaled a, Scaled b, bool& overflowed)
{
#if defined(__GNUC__) || defined(__clang__)
    Scaled sum = 0;
    if (__builtin_add_overflow(a, b, &sum)) {
        overflowed = true;
        return a < 0 ? -SCALED_MAX : SCALED_MAX;
    }
    return sum;
#else
    const Scaled sum = static_cast<Scaled>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
    if (((a ^ sum) & (b ^ sum)) < 0) {
        overflowed = true;
        return a < 0 ? -SCALED_MAX : SCALED_MAX;
    }
    return sum;
#endif
}

// a - b, on the same terms.
constexpr Scaled sub_checked(Scaled a, Scaled b, bool& overflowed)
{
#if defined(__GNUC__) || defined(__clang__)
    Scaled difference = 0;
    if (__builtin_sub_overflow(a, b, &difference)) {
        overflowed = true;
        return a < 0 ? -SCALED_MAX : SCALED_MAX;
    }
    return difference;
#else
    const Scaled difference =
        static_cast<Scaled>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
    if (((a ^ b) & (a ^ difference)) < 0) {
        overflowed = true;
        return a < 0 ? -SCALED_MAX : SCALED_MAX;
    }
    return difference;
#endif
}

// The same two for a caller with nowhere to report to.
constexpr Scaled add_saturating(Scaled a, Scaled b)
{
    bool ignored = false;
    return add_checked(a, b, ignored);
}
constexpr Scaled sub_saturating(Scaled a, Scaled b)
{
    bool ignored = false;
    return sub_checked(a, b, ignored);
}

// --- Arithmetic that matches Python ---------------------------------------------
//
// Not tex.web's. Pascal's div truncates toward zero and its round goes away
// from zero on a tie (the second is pascal_round in src/shipout.h, the one
// place this port rounds as tex.web does).

// Python's round() of a float: to nearest, ties to even. std::round and lround
// go away from zero on a tie and (int)(x + 0.5) truncates, and exact halves are
// routine here. Relies on the default rounding mode, as the rest of the double
// arithmetic in this library does.
inline double py_round(double value)
{
    return std::nearbyint(value);
}

// py_round() as an integer. Python raises for NaN and infinities and returns an
// unbounded integer for anything else; this answers 0 for NaN and clamps to
// +-SCALED_LIMIT.
inline Scaled round_to_scaled(double value)
{
    if (value != value)
        return 0;
    const double rounded = std::nearbyint(value);
    const double limit = static_cast<double>(SCALED_LIMIT);
    if (rounded >= limit)
        return SCALED_LIMIT;
    if (rounded <= -limit)
        return -SCALED_LIMIT;
    return static_cast<Scaled>(rounded);
}

// Python's `//`: the quotient rounded toward negative infinity. C++'s `/`
// truncates, which differs whenever the operands have opposite signs and the
// division is inexact. A zero divisor is a contract violation answered with 0;
// the one overflowing quotient, INT64_MIN // -1, answers INT64_MAX.
inline int64_t floor_div(int64_t a, int64_t b)
{
    if (!IMTYPESET_CONTRACT(b != 0, "floor_div: division by zero"))
        return 0;
    if (b == -1)
        return a == std::numeric_limits<int64_t>::min() ? std::numeric_limits<int64_t>::max() : -a;
    const int64_t quotient = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? quotient - 1 : quotient;
}

// The quotient rounded toward positive infinity, which the reference spells
// -((-a) // b). Same two special cases as floor_div.
inline int64_t ceil_div(int64_t a, int64_t b)
{
    if (!IMTYPESET_CONTRACT(b != 0, "ceil_div: division by zero"))
        return 0;
    if (b == -1)
        return a == std::numeric_limits<int64_t>::min() ? std::numeric_limits<int64_t>::max() : -a;
    const int64_t quotient = a / b;
    return (a % b != 0 && ((a < 0) == (b < 0))) ? quotient + 1 : quotient;
}

// Python's `%`: the remainder that goes with floor_div, taking the divisor's
// sign. A zero divisor is a contract violation answered with 0.
inline int64_t floor_mod(int64_t a, int64_t b)
{
    if (!IMTYPESET_CONTRACT(b != 0, "floor_mod: division by zero"))
        return 0;
    if (b == -1)
        return 0;
    const int64_t remainder = a % b;
    return (remainder != 0 && ((remainder < 0) != (b < 0))) ? remainder + b : remainder;
}

// A dimension from outside brought inside the invariant this header states.
// RUNNING passes through.
constexpr Scaled clamp_dimen(Scaled length)
{
    if (length == RUNNING)
        return RUNNING;
    return length > MAX_DIMEN ? MAX_DIMEN : (length < -MAX_DIMEN ? -MAX_DIMEN : length);
}

// --- Conversions -------------------------------------------------------------------
//
// Each float expression is evaluated in the reference's order, in doubles: the
// resolution ratio is formed first and the multiplication by UNITY comes last.

// Printer's points to scaled points, rounding once.
inline Scaled sp_from_pt(double points)
{
    return round_to_scaled(points * static_cast<double>(UNITY));
}

// Scaled points to printer's points. Exact for every Scaled a double holds.
inline double pt_from_sp(Scaled length)
{
    return static_cast<double>(length) / static_cast<double>(UNITY);
}

// Big points (1/72 inch) to scaled points.
inline Scaled sp_from_bp(double big_points)
{
    return round_to_scaled(big_points * (PT_PER_INCH / BP_PER_INCH) * static_cast<double>(UNITY));
}

// Device pixels to scaled points at a display resolution. A resolution of zero
// divides to an infinity (the reference raises), which clamps.
inline Scaled sp_from_px(double pixels, double px_per_inch = DEFAULT_PX_PER_INCH)
{
    return round_to_scaled(pixels * (PT_PER_INCH / px_per_inch) * static_cast<double>(UNITY));
}

// Scaled points to device pixels, without rounding. Rounding a position to the
// pixel grid happens once, on a final absolute coordinate.
inline double px_from_sp(Scaled length, double px_per_inch = DEFAULT_PX_PER_INCH)
{
    return static_cast<double>(length) * (px_per_inch / PT_PER_INCH) / static_cast<double>(UNITY);
}

// An absolute ship-out coordinate snapped to the pixel grid, ties to even. Not
// the glue rounding of tex.web section 625, which is pascal_round in shipout.
inline int64_t round_to_px(Scaled length, double px_per_inch = DEFAULT_PX_PER_INCH)
{
    return round_to_scaled(px_from_sp(length, px_per_inch));
}

} // namespace ImTypeset::detail
