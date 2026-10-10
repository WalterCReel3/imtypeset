// badness(t, s): how bad it is to stretch or shrink by t when s is available.
// Mirrors advtext/badness.py.
//
// WHAT OF tex.web THIS IS. Section 108, the function badness and the constant
// inf_bad, in part 7, "Arithmetic with scaled dimensions". Transcribed whole;
// the one thing added is the answer for a negative t, which the section
// excludes by its stated precondition (t >= 0).
//
// The integer approximation is the specification and not an implementation
// detail: it returns exactly 12 when half the available stretch is used and
// exactly 100 when all of it is, and the line breaker's fitness classes are
// thresholds against those values. 100 * (t / s)^3 computed any other way moves
// a handful of results across the thresholds and with them the line breaks.

#pragma once

#include "units.h"

namespace ImTypeset::detail
{

// Infinitely bad: tex.web's inf_bad (section 108). The largest value badness()
// returns short of this is 8189.
constexpr int INF_BAD = 10000;

// The badness of making up a total `t` out of amounts summing to `s`, in
// [0, INF_BAD]. `t` is what has to be made up; a caller measuring shrink
// negates its shortfall first. Zero or negative `s` is infinitely bad unless
// nothing has to be made up.
//
// A negative `t` is a contract violation (the reference raises; section 108
// states t >= 0 and does not test it) and answers INF_BAD without reaching the
// divisions, which agree with Pascal's div only because their operands are
// not negative.
int badness(Scaled t, Scaled s);

} // namespace ImTypeset::detail
