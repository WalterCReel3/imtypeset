// src/badness against tests/golden/core/badness.golden, and the claims of the
// reference's tests/test_badness.py that the golden does not already hold.

#include "core_golden.h"

#include "badness.h"

#include <set>
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

// With s = 297 the approximation collapses to r = t, which makes r directly
// addressable.
constexpr Scaled S_MAKING_R_EQUAL_T = 297;

} // namespace

TEST(badness_matches_the_reference)
{
    golden::File file;
    REQUIRE(file.load(golden::path("core/badness.golden")));
    const std::vector<core_golden::Case> cases = core_golden::cases(file, "core/badness");
    REQUIRE(!cases.empty());

    int replayed = 0;
    int rejected = 0;
    for (const core_golden::Case& c : cases) {
        CHECK_CONTEXT("case %s", c.name.c_str());
        core_golden::Reader reader(file, c);
        while (!reader.done()) {
            const golden::Record& record = reader.take();
            CHECK_CONTEXT("%s:%d", record.file, record.line);
            if (record.is("const")) {
                CHECK_EQ(std::string(record.raw(0)), std::string("INF_BAD"));
                CHECK_EQ(record.i64(2), int64_t(INF_BAD));
            } else if (record.is("badness")) {
                CHECK_EQ(int64_t(badness(record.i64(0), record.i64(1))), record.i64(2));
                ++replayed;
            } else if (record.is("badness_raises")) {
                // The reference raises. The port reports a contract violation
                // and answers INF_BAD without dividing.
                const ContractHandler before = contract_handler();
                contract_handler() = &count_violation;
                g_violations = 0;
                CHECK_EQ(badness(record.i64(0), record.i64(1)), INF_BAD);
                CHECK_EQ(g_violations, 1);
                contract_handler() = before;
                ++rejected;
            } else {
                FAIL("badness.golden holds a record this test does not replay");
            }
        }
    }
    CHECK(replayed > 2500);
    CHECK_EQ(rejected, 4);
}

TEST(badness_can_produce_exactly_the_1095_values_section_108_claims)
{
    std::set<int> produced;
    int largest_finite = 0;
    for (Scaled r = 0; r <= 1291; ++r) {
        const int b = badness(r, S_MAKING_R_EQUAL_T);
        produced.insert(b);
        if (b != INF_BAD && b > largest_finite)
            largest_finite = b;
    }
    CHECK_EQ(produced.size(), size_t(1095));
    CHECK(produced.count(INF_BAD) == 1);
    CHECK_EQ(largest_finite, 8189);
    CHECK(largest_finite < (1 << 13));
}

TEST(badness_is_monotone_in_both_arguments)
{
    // Section 108 proves badness(t+1, s) >= badness(t, s) >= badness(t, s+1).
    {
        const Scaled s = 65536;
        int previous = 0;
        for (Scaled t = 0; t < 3 * s; t += 97) {
            const int current = badness(t, s);
            if (!CHECK(current >= previous))
                break;
            previous = current;
        }
    }
    {
        const Scaled t = 65536;
        int previous = INF_BAD;
        for (Scaled s = 1; s < 4 * t; s += 211) {
            const int current = badness(t, s);
            if (!CHECK(current <= previous))
                break;
            previous = current;
        }
    }
}

TEST(badness_stays_in_range_across_a_wide_sweep)
{
    for (Scaled t = 0; t < 20000000; t += 137117) {
        for (const Scaled s :
             {Scaled(0), Scaled(1), Scaled(297), Scaled(65536), Scaled(1663497), Scaled(9000000)}) {
            const int b = badness(t, s);
            if (!CHECK(b >= 0 && b <= INF_BAD))
                return;
        }
    }
}

TEST(dividing_before_multiplying_decides_feasibility_at_the_boundary)
{
    // s = 297 * 5601 + 296 loses the most in `s / 297`; t = 5601 * 1291 is past
    // the direct-multiply limit. TeX's path gives r = 1291, infinitely bad;
    // multiplying first would give r = 1290 and 8189.
    const Scaled s = 1663793;
    const Scaled t = 7230891;
    CHECK_EQ(s / 297, Scaled(5601));
    CHECK_EQ(s % 297, Scaled(296));
    CHECK_EQ(t, Scaled(5601) * 1291);
    CHECK_EQ(badness(t, s), INF_BAD);
    CHECK_EQ((t * 297) / s, Scaled(1290));
    CHECK_EQ(badness(8000000, 2000000), badness(4, 1));
    CHECK_EQ(badness(8000000, 2000000), 6396);
}
