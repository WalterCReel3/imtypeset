#include "badness.h"

namespace ImTypeset::detail
{

namespace
{

// The constants of tex.web section 108, which writes them as literals.
//
// The multiplier in r ~ ALPHA * t / s, chosen so that ALPHA^3 is close to
// 100 * 2^18; r is roughly the cube root of the wanted badness, scaled.
constexpr Scaled ALPHA = 297;

// The largest t for which t * 297 fits a signed 32-bit word. Above it the
// source divides before it multiplies. A 64-bit product would not overflow, but
// the two paths round differently, and the branch is what agrees with TeX on
// the large t a stretched line produces.
constexpr Scaled MAX_T_FOR_DIRECT_MULTIPLY = 7230584;

// Below this a t too large to multiply cannot be scaled down usefully either,
// and the source sets r := t, which is past MAX_R.
constexpr Scaled MIN_S_FOR_SCALED_DIVIDE = 1663497;

// 1290^3 < 2^31 < 1291^3. Past it the result is infinitely bad anyway.
constexpr Scaled MAX_R = 1290;

constexpr Scaled ROUNDING_ADDEND = Scaled(1) << 17;
constexpr Scaled SCALE = Scaled(1) << 18;

} // namespace

// tex.web section 108, statement for statement; t, s and r are its names.
int badness(Scaled t, Scaled s)
{
    if (!IMTYPESET_CONTRACT(t >= 0, "badness: t must not be negative"))
        return INF_BAD;
    if (t == 0)
        return 0;
    if (s <= 0)
        return INF_BAD;

    // Every operand below is positive, so `/` is the reference's `//` and
    // section 108's div alike.
    Scaled r;
    if (t <= MAX_T_FOR_DIRECT_MULTIPLY)
        r = (t * ALPHA) / s;
    else if (s >= MIN_S_FOR_SCALED_DIVIDE)
        r = t / (s / ALPHA);
    else
        r = t;

    if (r > MAX_R)
        return INF_BAD;
    return static_cast<int>((r * r * r + ROUNDING_ADDEND) / SCALE);
}

} // namespace ImTypeset::detail
