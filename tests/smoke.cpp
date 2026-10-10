// What every other test stands on: the library links and saw the ImGui this
// file sees, an ImGui frame runs with no window, no display and no renderer
// backend, the harness in check.h reports what it should, golden.h reads the
// golden format, and the fonts the reference uses are where tests look for them.

#include "imtypeset.h"

#include "check.h"
#include "golden.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

// The library and this file were compiled against the same ImGui, and it is one
// the header accepts. Which release that is belongs to the build: the tested
// one by default, any at or above the floor when a checkout is named.
TEST(library_reports_its_version_and_the_imgui_it_was_built_against)
{
    CHECK_EQ(ImTypeset::GetVersion(), IMTYPESET_VERSION);
    CHECK_EQ(ImTypeset::GetImGuiVersionNum(), IMGUI_VERSION_NUM);
    CHECK(ImTypeset::GetImGuiVersionNum() >= IMTYPESET_IMGUI_VERSION_NUM_MIN);
    CHECK(IMTYPESET_IMGUI_VERSION_NUM >= IMTYPESET_IMGUI_VERSION_NUM_MIN);
    std::printf("smoke: Dear ImGui %d (tested: %d, floor: %d)\n", IMGUI_VERSION_NUM,
                IMTYPESET_IMGUI_VERSION_NUM, IMTYPESET_IMGUI_VERSION_NUM_MIN);
}

// ImWchar is 32 bits when the imgui target was built with IMGUI_USE_WCHAR32 and
// 16 otherwise, and the library has to work with both. What is checked is that
// this translation unit sees the width the build asked for: the define changes
// the layout of ImGui's types, so a target that missed it would not fail to
// link, it would misbehave.
TEST(imwchar_is_the_width_the_build_asked_for)
{
#ifdef IMGUI_USE_WCHAR32
    const bool wide = true;
#else
    const bool wide = false;
#endif
    CHECK_EQ(sizeof(ImWchar), wide ? 4u : 2u);
    CHECK_EQ(IM_UNICODE_CODEPOINT_MAX, wide ? 0x10FFFF : 0xFFFF);
#ifdef IMTYPESET_EXPECT_WCHAR32
    CHECK_EQ(wide, IMTYPESET_EXPECT_WCHAR32 != 0);
#endif
    std::printf("smoke: ImWchar is %zu bits\n", sizeof(ImWchar) * 8);
}

TEST(headless_frame_emits_vertices)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // write nothing into the working directory
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    io.DeltaTime = 1.0f / 60.0f;
    // What a 1.92 renderer backend would announce. Without it NewFrame expects
    // the atlas to have been built the pre-1.92 way.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    // More than one frame: a window is hidden on the frame it first appears,
    // while it is auto-sized, and emits no vertices then.
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        ImGui::Begin("smoke");
        ImGui::TextUnformatted("imtypeset");
        ImGui::End();
        ImGui::Render();
    }

    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (CHECK(draw_data != nullptr))
        CHECK(draw_data->TotalVtxCount > 0);

    // The rasteriser is a property of how the imgui target was built, and
    // glyph boxes depend on it, so a test that compares them has to know.
    const char* loader = io.Fonts->FontLoaderName;
    if (CHECK(loader != nullptr)) {
#ifdef IMGUI_ENABLE_FREETYPE
        CHECK(std::strncmp(loader, "FreeType", 8) == 0);
#else
        CHECK(std::strncmp(loader, "stb_truetype", 12) == 0);
#endif
        std::printf("smoke: imtypeset %s, ImGui %s, font loader %s\n", ImTypeset::GetVersion(),
                    IMGUI_VERSION, loader);
    }

    ImGui::DestroyContext();
}

// --- check.h ---------------------------------------------------------------------
// The harness is checked by running its comparison and printing functions
// directly: a CHECK that is meant to fail cannot be used to test itself without
// failing the run.

TEST(check_equal_compares_integers_by_value)
{
    CHECK(check::equal(3, 3u));
    CHECK(check::equal(size_t{3}, 3));
    CHECK(!check::equal(-1, std::numeric_limits<unsigned>::max()));
    CHECK(!check::equal(std::numeric_limits<uint64_t>::max(), int64_t{-1}));
    CHECK(check::equal(int64_t{-5}, -5));
    CHECK(check::equal('a', 97));
}

TEST(check_equal_compares_strings_by_content)
{
    const std::string s = "glue";
    char buffer[] = "glue";
    const char* null_text = nullptr;
    CHECK(check::equal(s, "glue"));
    CHECK(check::equal("glue", s));
    CHECK(check::equal(buffer, "glue"));
    CHECK(check::equal(std::string_view("glue"), buffer));
    CHECK(!check::equal(s, "glu"));
    CHECK(!check::equal(null_text, "glue"));
    CHECK(!check::equal(s, null_text));
    CHECK(check::equal(null_text, nullptr));
}

TEST(check_to_text_prints_what_a_failure_needs)
{
    enum class Kind { a, b = 7 };
    CHECK_EQ(check::to_text(-42), "-42");
    CHECK_EQ(check::to_text(uint64_t{18446744073709551615ull}), "18446744073709551615");
    CHECK_EQ(check::to_text(true), "true");
    CHECK_EQ(check::to_text(Kind::b), "7");
    CHECK_EQ(check::to_text(std::string("a\"b\n")), "\"a\\\"b\\x0a\"");
    CHECK_EQ(check::to_text("lit"), "\"lit\"");
    // Decimal to read, hex because goldens compare the last bit.
    CHECK_EQ(check::to_text(1.5), "1.5 (0x1.8p+0)");
}

TEST(check_near_rejects_nan)
{
    CHECK_NEAR(1.0, 1.0 + 1e-12, 1e-9);
    bool nan_passed = true;
    bool far_passed = true;
    long failures_recorded = 0;
    {
        const check::ExpectFailures expected;
        nan_passed = CHECK_NEAR(std::nan(""), 0.0, 1.0);
        far_passed = CHECK_NEAR(1.0, 3.0, 1.0);
        failures_recorded = expected.count();
    }
    CHECK(!nan_passed);
    CHECK(!far_passed);
    CHECK_EQ(failures_recorded, 2);
}

// --- golden.h --------------------------------------------------------------------

TEST(golden_percent_decoding)
{
    std::string out = "stale";
    CHECK(golden::percent_decode("%", out));
    CHECK_EQ(out, "");
    CHECK(golden::percent_decode("a%20b%25c", out));
    CHECK_EQ(out, "a b%c");
    CHECK(golden::percent_decode("%e2%88%91", out)); // lower-case hex, U+2211
    CHECK_EQ(out, "\xE2\x88\x91");
    CHECK(golden::percent_decode("%00x", out)); // an encoded NUL is a byte like any other
    CHECK_EQ(out.size(), 2u);
    CHECK(!golden::percent_decode("%2", out));
    CHECK(!golden::percent_decode("abc%", out));
    CHECK(!golden::percent_decode("%zz", out));
}

TEST(golden_percent_encoding_round_trips)
{
    const std::string cases[] = {
        "", " ", "%", "plain", "a b\tc\n", "\xE2\x88\x91 x", "#lead", std::string("nul\0in", 6)};
    for (const std::string& text : cases) {
        const std::string token = golden::percent_encode(text);
        CHECK(!token.empty());
        CHECK(token.find(' ') == std::string::npos);
        std::string back;
        CHECK(golden::percent_decode(token, back));
        CHECK_EQ(back, text);
    }
    CHECK_EQ(golden::percent_encode(""), "%");
    CHECK_EQ(golden::percent_encode("%"), "%25");
}

TEST(golden_file_splits_records_and_skips_comments)
{
    golden::File file;
    file.parse("# a comment\n"
               "\n"
               "case first%20case\r\n"
               "   # an indented comment\n"
               "glue 65536 -32768 0x1.8000000000000p+1 %\n"
               "  kern\t7   #not-a-comment\n"
               "last 1",
               "<memory>");
    const std::vector<golden::Record>& records = file.records();
    REQUIRE_EQ(records.size(), 4u);

    CHECK(records[0].is("case"));
    CHECK_EQ(records[0].line, 3);
    CHECK_EQ(records[0].size(), 1u);
    CHECK_EQ(records[0].str(0), "first case");

    CHECK_EQ(records[1].tag, "glue");
    CHECK_EQ(records[1].line, 5);
    REQUIRE_EQ(records[1].size(), 4u);
    CHECK_EQ(records[1].i64(0), 65536);
    CHECK_EQ(records[1].i32(1), -32768);
    CHECK_EQ(records[1].f64(2), 3.0);
    CHECK_EQ(records[1].str(3), "");

    // '#' starts a comment only at the start of a line.
    CHECK_EQ(records[2].tag, "kern");
    REQUIRE_EQ(records[2].size(), 2u);
    CHECK_EQ(records[2].raw(1), "#not-a-comment");

    // The last line needs no newline.
    CHECK_EQ(records[3].tag, "last");
    CHECK(records[3].boolean(0));
}

TEST(golden_floats_are_read_bit_for_bit)
{
    // Tokens exactly as Python's float.hex() prints them.
    golden::File file;
    file.parse("f 0x1.999999999999ap-4 -0x0.0p+0 0x0.0000000000001p-1022 0x1.fffffffffffffp+1023 "
               "inf -inf nan 0x1.0000000000000p+0\n",
               "<memory>");
    REQUIRE_EQ(file.records().size(), 1u);
    golden::Cursor c(file.records()[0]);
    CHECK_EQ(c.f64(), 0.1);
    const double negative_zero = c.f64();
    CHECK_EQ(negative_zero, 0.0);
    CHECK(std::signbit(negative_zero));
    CHECK_EQ(c.f64(), std::numeric_limits<double>::denorm_min());
    CHECK_EQ(c.f64(), std::numeric_limits<double>::max());
    CHECK_EQ(c.f64(), std::numeric_limits<double>::infinity());
    CHECK_EQ(c.f64(), -std::numeric_limits<double>::infinity());
    CHECK(std::isnan(c.f64()));
    CHECK_EQ(c.remaining(), 1u);
    CHECK_EQ(c.f64(), 1.0);
    CHECK(c.expect_done());
}

TEST(golden_integers_cover_64_bits)
{
    golden::File file;
    file.parse("i 9223372036854775807 -9223372036854775808 1114111 0\n", "<memory>");
    REQUIRE_EQ(file.records().size(), 1u);
    const golden::Record& r = file.records()[0];
    CHECK_EQ(r.i64(0), std::numeric_limits<int64_t>::max());
    CHECK_EQ(r.i64(1), std::numeric_limits<int64_t>::min());
    CHECK_EQ(r.u32(2), 0x10FFFFu);
    CHECK(!r.boolean(3));
}

// A broken golden has to fail the test that reads it: each getter reports
// exactly once and returns its zero.
TEST(golden_malformed_fields_are_failures)
{
    golden::File file;
    file.parse("bad 12x 99999999999999999999 0x1.8q %4 -1 4294967296 2\n", "<memory>");
    REQUIRE_EQ(file.records().size(), 1u);
    const golden::Record& r = file.records()[0];

    int64_t not_integer = 1, too_big = 1, past_the_end = 1;
    double not_float = 1.0;
    std::string bad_escape = "stale";
    uint32_t negative = 1, too_wide = 1;
    bool not_boolean = true, done_early = true;
    std::vector<long> reported;
    {
        const check::ExpectFailures expected;
        long seen = 0;
        const auto note = [&]() {
            reported.push_back(expected.count() - seen);
            seen = expected.count();
        };
        not_integer = r.i64(0);
        note();
        too_big = r.i64(1);
        note();
        not_float = r.f64(2);
        note();
        bad_escape = r.str(3);
        note();
        negative = r.u32(4);
        note();
        too_wide = r.u32(5);
        note();
        not_boolean = r.boolean(6);
        note();
        past_the_end = r.i64(7);
        note();
        const golden::Cursor c(r, 6);
        done_early = c.expect_done();
        note();
    }

    CHECK_EQ(not_integer, 0);
    CHECK_EQ(too_big, 0);
    CHECK_EQ(not_float, 0.0);
    CHECK_EQ(bad_escape, "");
    CHECK_EQ(negative, 0u);
    CHECK_EQ(too_wide, 0u);
    CHECK(!not_boolean);
    CHECK_EQ(past_the_end, 0);
    CHECK(!done_early);
    REQUIRE_EQ(reported.size(), 9u);
    for (const long n : reported)
        CHECK_EQ(n, 1);
}

TEST(golden_missing_file_is_a_failure)
{
    golden::File file;
    bool loaded = true;
    long failures_recorded = 0;
    {
        const check::ExpectFailures expected;
        loaded = file.load(golden::path("no-such-directory/no-such-file.golden"));
        failures_recorded = expected.count();
    }
    CHECK(!loaded);
    CHECK_EQ(failures_recorded, 1);
    CHECK(file.records().empty());
    CHECK(golden::list("no-such-directory").empty());
}

// --- tests/data ------------------------------------------------------------------

// The files advtext.fontlib.font_file() resolves for the reference's faces, and
// DroidSans as a face the reference does not bundle. Sizes are those of the
// copies taken from imgui-bundle 1.92.900; tests/data/fonts/LICENSES.md has
// their hashes.
TEST(test_fonts_are_present)
{
    struct Font {
        const char* name;
        size_t size;
        uint32_t sfnt_version;
    };
    const Font fonts[] = {
        {"Roboto-Regular.ttf", 168260, 0x00010000},
        {"Roboto-Bold.ttf", 167336, 0x00010000},
        {"Roboto-RegularItalic.ttf", 170504, 0x00010000},
        {"Roboto-BoldItalic.ttf", 171508, 0x00010000},
        {"Inconsolata-Medium.ttf", 0, 0x00010000},
        {"DroidSans.ttf", 0, 0x00010000},
        {"latinmodern-math.otf", 733736, 0x4F54544F}, // 'OTTO': CFF outlines
    };
    for (const Font& font : fonts) {
        CHECK_CONTEXT("font %s", font.name);
        std::vector<unsigned char> bytes;
        if (!CHECK(golden::read_file(golden::data_path(std::string("fonts/") + font.name), bytes)))
            continue;
        if (font.size != 0)
            CHECK_EQ(bytes.size(), font.size);
        if (!CHECK(bytes.size() >= 12))
            continue;
        const uint32_t version = (uint32_t{bytes[0]} << 24) | (uint32_t{bytes[1]} << 16) |
                                 (uint32_t{bytes[2]} << 8) | uint32_t{bytes[3]};
        CHECK_EQ(version, font.sfnt_version);
    }
}
