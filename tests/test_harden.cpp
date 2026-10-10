// The second hardening pass: the formula parser and the selection model (items
// H1 to H5 and H8 of docs/hardening.md) and the draw loop (D3, D4, D6, D7),
// one or more tests each. Every test here began as the smallest input that
// showed the item, under ASan + UBSan where that is what shows it, and is kept
// as what holds the fix or the measurement.

#include "golden.h"
#include "math_golden.h"

#include "imtypeset.h"

#include "context.h"
#include "draw.h"
#include "mathtext.h"
#include "selection.h"
#include "shipout.h"

#include "imgui_internal.h" // ImDrawListSharedData, the atlas builder's bakes

#include "support/imgui_compat.h"
#include "support/soft_raster.h"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

// --- Counting what is allocated ---------------------------------------------------
// Every form of operator new, in bytes asked for. Not inlined: GCC, seeing
// through an inlined replacement to malloc and free, reports the pair as
// mismatched.
#if defined(__GNUC__)
#define TEST_HARDEN_NOINLINE __attribute__((noinline))
#else
#define TEST_HARDEN_NOINLINE
#endif

static size_t g_bytes_asked = 0;

TEST_HARDEN_NOINLINE void* operator new(size_t size)
{
    g_bytes_asked += size;
    void* const memory = std::malloc(size != 0 ? size : 1);
    if (memory == nullptr)
        std::abort();
    return memory;
}
TEST_HARDEN_NOINLINE void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    g_bytes_asked += size;
    return std::malloc(size != 0 ? size : 1);
}
TEST_HARDEN_NOINLINE void* operator new[](size_t size)
{
    return operator new(size);
}
TEST_HARDEN_NOINLINE void* operator new[](size_t size, const std::nothrow_t& tag) noexcept
{
    return operator new(size, tag);
}
TEST_HARDEN_NOINLINE void operator delete(void* memory) noexcept
{
    std::free(memory);
}
TEST_HARDEN_NOINLINE void operator delete(void* memory, size_t) noexcept
{
    std::free(memory);
}
TEST_HARDEN_NOINLINE void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}
TEST_HARDEN_NOINLINE void operator delete[](void* memory) noexcept
{
    std::free(memory);
}
TEST_HARDEN_NOINLINE void operator delete[](void* memory, size_t) noexcept
{
    std::free(memory);
}
TEST_HARDEN_NOINLINE void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

namespace
{

std::string repeated(const char* piece, size_t times)
{
    std::string out;
    out.reserve(std::strlen(piece) * times);
    for (size_t i = 0; i < times; ++i)
        out += piece;
    return out;
}

bool starts_with(const std::string& text, const char* prefix)
{
    return text.rfind(prefix, 0) == 0;
}

const struct {
    ImTypesetFace face;
    const char* file;
} DEFAULT_FONTS[] = {
    {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
    {ImTypesetFace_Bold, "Roboto-Bold.ttf"},
    {ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
    {ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
    {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
    {ImTypesetFace_Math, "latinmodern-math.otf"},
};

// A headless ImGui context with an ImTypeset context in it and the
// reference's default fonts.
struct Stage {
    Stage()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        if (!IMTYPESET_CHECKVERSION())
            FAIL("IMTYPESET_CHECKVERSION: %s", ImTypeset::GetLastError()->Message);
        ctx = ImTypeset::CreateContext();
        if (ctx == nullptr) {
            FAIL("CreateContext: %s", ImTypeset::GetLastError()->Message);
            return;
        }
        for (const auto& entry : DEFAULT_FONTS) {
            const std::string path = golden::data_path(std::string("fonts/") + entry.file);
            if (ImTypeset::AddFontFromFileTTF(entry.face, path.c_str()) == nullptr)
                FAIL("%s: %s", entry.file, ImTypeset::GetLastError()->Message);
        }
        ImTypeset::GetStyle().EmSize = 16.0f;
        ImTypeset::GetStyle().Color = IM_COL32(0, 0, 0, 255);
    }
    ~Stage()
    {
        ImTypeset::DestroyContext();
        ImGui::DestroyContext();
    }
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    ImTypesetContext* ctx = nullptr;
};

struct Frame {
    Frame() { ImGui::NewFrame(); }
    ~Frame() { ImGui::Render(); }
};

// Sets LC_NUMERIC to a locale that writes a decimal comma, and puts back what
// was there. `name` is null when the machine has none of the ones tried.
struct DecimalComma {
    DecimalComma()
    {
        const char* const now = std::setlocale(LC_NUMERIC, nullptr);
        before = now != nullptr ? now : "C";
        static const char* const candidates[] = {
            "de_DE.UTF-8", "de_DE.utf8",  "de_DE",       "fr_FR.UTF-8", "fr_FR.utf8",     "fr_FR",
            "nl_NL.UTF-8", "es_ES.UTF-8", "it_IT.UTF-8", "ru_RU.UTF-8", "German_Germany",
        };
        for (const char* candidate : candidates) {
            if (std::setlocale(LC_NUMERIC, candidate) == nullptr)
                continue;
            char probe[16];
            std::snprintf(probe, sizeof probe, "%.2f", 1.25);
            if (std::strcmp(probe, "1.25") != 0) {
                name = candidate;
                return;
            }
        }
        std::setlocale(LC_NUMERIC, before.c_str());
    }
    ~DecimalComma() { std::setlocale(LC_NUMERIC, before.c_str()); }
    DecimalComma(const DecimalComma&) = delete;
    DecimalComma& operator=(const DecimalComma&) = delete;

    std::string before;
    const char* name = nullptr;
};

} // namespace

// --- H1: a formula too long, or too large --------------------------------------------

TEST(h1_a_source_past_the_stated_length_is_refused_before_it_is_read)
{
    Formula formula;
    Refusal refusal;
    // At the bound it is read like any other, and what the bound is for is
    // what that costs: 208 bytes of heap asked for per byte of source when
    // this was written, and held here to under 512.
    {
        const std::string longest(MAX_SOURCE_BYTES, 'x');
        const size_t before = g_bytes_asked;
        REQUIRE(parse(longest, false, formula, refusal));
        const size_t asked = g_bytes_asked - before;
        CHECK_EQ(formula.view().size, MAX_SOURCE_BYTES);
        std::printf("    %zu bytes of x: %zu bytes asked for, %.0f a byte\n", longest.size(), asked,
                    static_cast<double>(asked) / static_cast<double>(longest.size()));
        CHECK(asked < 512 * MAX_SOURCE_BYTES);
    }
    // One byte past it, and twice it: refused for what it is, at the cost of
    // nothing. The reference reads both. No larger than that: a parser with
    // no bound spends some hundreds of bytes on each one it is given, and this
    // test has to be able to fail without taking the machine with it.
    for (const size_t size : {MAX_SOURCE_BYTES + 1, 2 * MAX_SOURCE_BYTES}) {
        CHECK_CONTEXT("%zu bytes", size);
        const std::string source(size, 'x');
        const size_t before = g_bytes_asked;
        CHECK(!parse(source, false, formula, refusal));
        const size_t asked = g_bytes_asked - before;
        CHECK(asked < 4096);
        CHECK(refusal.kind == RefusalKind::LAYOUT);
        CHECK_EQ(refusal.reason, "the formula is too large to set: it is longer than " +
                                     std::to_string(MAX_SOURCE_BYTES) +
                                     " bytes, which is the longest that is read");
        CHECK_EQ(refusal.position, size_t(0));
        CHECK_EQ(refusal.byte_position, size_t(0));
        CHECK(formula.view().empty());
    }
    // The bound is on bytes, whatever they decode to.
    CHECK(!parse(repeated("\xC3\xA9", MAX_SOURCE_BYTES / 2 + 1), false, formula, refusal));
    CHECK(refusal.kind == RefusalKind::LAYOUT);
}

TEST(h1_a_list_the_engine_finds_too_large_is_not_blamed_on_the_font)
{
    math_golden::FakeFonts fake("chained", "fake");
    MathFonts fonts;
    REQUIRE(fonts.init(18000, fake.math(), fake.text()));
    Formula formula;
    for (const MathError error : {MathError::TOO_LARGE, MathError::TOO_DEEP}) {
        MathStatus status;
        status.error = error;
        const Refusal refusal = refusal_from(status, "x", formula, fonts);
        CHECK(refusal.kind == RefusalKind::LAYOUT);
        CHECK(starts_with(refusal.reason, "the formula is too large to set: "));
        CHECK(refusal.reason.find("face") == std::string::npos);
        CHECK_EQ(refusal.position, size_t(0));
    }
    // What a list of more items than the engine translates gets, end to end:
    // twice the longest source the parser reads, as a list made by hand.
    MathList list;
    std::vector<MathItem> items(
        size_t(MAX_MATH_ITEMS) + 1,
        MathItem::atom(AtomType::ORD, Field::math_char('x'), Field(), Field(), Limits::DEFAULT));
    formula.list.clear();
    formula.items = formula.list.add_items(items);
    NodeArena arena;
    Node box;
    MathStatus status;
    CHECK(!set_formula(formula, fonts, arena, box, status));
    CHECK(status.error == MathError::TOO_LARGE);
    CHECK(refusal_from(status, "", formula, fonts).kind == RefusalKind::LAYOUT);
    CHECK_EQ(arena.box_count(), size_t(0));
}

TEST(h1_the_longest_sources_the_parser_reads_are_set_or_refused_for_what_they_are)
{
    // Sources of the longest length, each the densest way found of making
    // items out of bytes. Whatever the engine answers, it is not the font's
    // doing, and nothing is left behind.
    math_golden::FakeFonts fake("chained", "fake");
    MathFonts fonts;
    REQUIRE(fonts.init(18000, fake.math(), fake.text()));
    const char* const pieces[] = {"x", "'", "{}", "{}'", "^x", "x_x^x", "\\,", "\\frac12", "x'"};
    for (const char* piece : pieces) {
        CHECK_CONTEXT("pieces of %s", piece);
        const std::string source = repeated(piece, MAX_SOURCE_BYTES / std::strlen(piece));
        NodeArena arena;
        Formula formula;
        Refusal refusal;
        Node box;
        if (typeset(source, false, fonts, arena, formula, box, refusal)) {
            CHECK(arena.box(box).width <= MAX_DIMEN);
            continue;
        }
        std::printf("    %s: %s: %s\n", piece, refusal_kind_value(refusal.kind),
                    refusal.reason.c_str());
        CHECK(refusal.kind != RefusalKind::FONT);
        CHECK_EQ(arena.box_count(), size_t(0));
    }
}

TEST(h1_through_the_public_api_a_formula_too_long_is_refused_and_set_as_its_source)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const std::string text = "see $" + std::string(MAX_SOURCE_BYTES + 1, 'x') + "$ here";
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size(), nullptr, 0,
                                ImTypesetTextFlags_Math);
    ImTypeset::LayoutParagraph(p, 300.0f, nullptr);
    REQUIRE_EQ(ImTypeset::GetParagraphFormulaCount(p), 1);
    const ImTypesetFormula formula = ImTypeset::GetParagraphFormula(p, 0);
    CHECK(formula.Refused);
    CHECK(starts_with(formula.Reason, "the formula is too large to set: it is longer than "));
    CHECK_EQ(formula.BytePos, 5);
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, ImTypesetError_MathRefused);
    ImGuiTextBuffer copy;
    const ImTypesetTextRange all = {0, static_cast<int>(text.size())};
    ImTypeset::AppendParagraphRangeText(p, all, &copy);
    CHECK_EQ(std::string(copy.c_str()), text);
}

// --- H2: a formula wider than any dimension may be -----------------------------------

TEST(h2_a_formula_wider_than_max_dimen_is_refused_and_not_stored)
{
    FontLibrary library;
    REQUIRE(math_golden::register_reference_fonts(library));
    math_golden::LibraryFonts from_library(library);
    MathFonts fonts;
    // 12 pt: a \qquad is about 24 pt, and 25,000 of them are 36 times MAX_DIMEN.
    REQUIRE(fonts.init(12 * UNITY, from_library.math(), from_library.text()));
    NodeArena arena;
    Formula formula;
    Refusal refusal;
    Node box;
    const std::string wide = repeated("\\qquad", 25000);
    REQUIRE(wide.size() <= MAX_SOURCE_BYTES);
    CHECK(!typeset(wide, false, fonts, arena, formula, box, refusal));
    CHECK(refusal.kind == RefusalKind::LAYOUT);
    CHECK_EQ(refusal.reason,
             std::string("the formula is too large to set: its box is wider or taller than the "
                         "largest dimension a box can have"));
    CHECK_EQ(arena.box_count(), size_t(0));

    // The widest that is inside the bound sets, and is inside it; one more
    // space is outside it. A \qquad is what this face makes of 36 mu.
    REQUIRE(typeset("\\qquad", false, fonts, arena, formula, box, refusal));
    const Scaled one = arena.box(box).width;
    REQUIRE(one > 20 * UNITY && one < 28 * UNITY);
    arena.clear();
    const size_t most = static_cast<size_t>(MAX_DIMEN / one);
    REQUIRE(typeset(repeated("\\qquad", most), false, fonts, arena, formula, box, refusal));
    CHECK_EQ(arena.box(box).width, static_cast<Scaled>(most) * one);
    CHECK(arena.box(box).width <= MAX_DIMEN);
    arena.clear();
    CHECK(!typeset(repeated("\\qquad", most + 1), false, fonts, arena, formula, box, refusal));
    CHECK(refusal.kind == RefusalKind::LAYOUT);
    CHECK_EQ(arena.box_count(), size_t(0));

    // A box inside the formula that is too wide, in a formula that is not:
    // negative spaces after it bring the whole back inside the bound.
    const std::string hidden =
        "\\overline{" + repeated("\\qquad", most + 1) + "}" + repeated("\\!", 12 * (most + 1));
    CHECK(!typeset(hidden, false, fonts, arena, formula, box, refusal));
    CHECK(refusal.kind == RefusalKind::LAYOUT);
    CHECK_EQ(arena.box_count(), size_t(0));
}

namespace
{

// Whether every dimension stored anywhere under a node is inside the bound.
bool all_dimensions_in_bound(const NodeArena& arena, const Node& node, int depth = 0)
{
    const auto inside = [](Scaled value) {
        return value == RUNNING || (value >= -MAX_DIMEN && value <= MAX_DIMEN);
    };
    if (node.type == NodeType::BOX) {
        const Box* box = arena.find_box(node);
        if (box == nullptr || depth > NodeArena::MAX_NESTING)
            return false;
        if (!inside(box->width) || !inside(box->height) || !inside(box->depth) ||
            !inside(box->shift_amount))
            return false;
        for (const Node& child : arena.view(box->contents)) {
            if (!all_dimensions_in_bound(arena, child, depth + 1))
                return false;
        }
        return true;
    }
    if (node.type == NodeType::KERN || node.type == NodeType::GLUE || node.type == NodeType::RULE)
        return inside(node.a) && inside(node.b) && inside(node.c);
    return true;
}

} // namespace

TEST(h2_at_the_largest_sizes_a_formula_is_set_inside_the_bound_or_refused_as_too_large)
{
    // The largest base size the fonts take is MAX_DIMEN itself, where one
    // letter is most of the largest box there can be and every parameter of
    // the math face is a large number. Whatever is set holds no dimension
    // past the bound, in a box, a kern, a glue or a rule; whatever is not is
    // refused for its size. Under the sanitisers, which is the other half.
    FontLibrary library;
    REQUIRE(math_golden::register_reference_fonts(library));
    const char* const sources[] = {
        "x",   "M",          "\\sum",         "\\frac{a}{b}",        "\\sqrt{x}",
        "x^2", "x_i^2",      "\\int_0^1",     "\\left(x\\right)",    "\\overline{x}",
        "a+b", "\\text{ab}", "\\binom{n}{k}", "\\sum_{i=1}^{n} i^2", "\\underline{x}",
    };
    long set = 0;
    long refused = 0;
    for (const Scaled size : {MAX_DIMEN, MAX_DIMEN / 2, MAX_DIMEN / 8, MAX_DIMEN / 64}) {
        math_golden::LibraryFonts from_library(library);
        MathFonts fonts;
        REQUIRE(fonts.init(size, from_library.math(), from_library.text()));
        for (const char* source : sources) {
            for (const bool display : {false, true}) {
                CHECK_CONTEXT("%s at %lld sp%s", source, static_cast<long long>(size),
                              display ? ", display" : "");
                NodeArena arena;
                Formula formula;
                Refusal refusal;
                Node box;
                if (typeset(source, display, fonts, arena, formula, box, refusal)) {
                    ++set;
                    CHECK(all_dimensions_in_bound(arena, box));
                } else {
                    ++refused;
                    CHECK(refusal.kind == RefusalKind::LAYOUT);
                    CHECK_EQ(arena.box_count(), size_t(0));
                }
            }
        }
    }
    std::printf("    %ld set, %ld refused for their size\n", set, refused);
    CHECK(set > 10);
    CHECK(refused > 10);
}

namespace
{

// A math face whose axis is a million ems below the baseline: what a MATH
// table can say, since nothing bounds a number in a file. The clearances of a
// fraction made with it are that large; the engine turns them into the
// fraction's shifts, and its box is then one no node may hold.
class FarAxis : public math_golden::FakeMath
{
public:
    explicit FarAxis(double size)
        : math_golden::FakeMath(size, false)
        , _far(math_golden::FakeMath::math())
    {
        _far.axis_height = -1.0e6 * size;
    }
    const MathParams& math() const override { return _far; }

private:
    MathParams _far;
};

const MathFontMetrics* far_axis(void*, Scaled size)
{
    static std::vector<std::unique_ptr<FarAxis>> made;
    made.push_back(std::make_unique<FarAxis>(static_cast<double>(size)));
    return made.back().get();
}

} // namespace

TEST(h2_a_math_face_with_a_parameter_out_of_all_proportion_puts_no_dimension_past_the_bound)
{
    MathFonts fonts;
    REQUIRE(fonts.init(18000, MathResolver{&far_axis, nullptr}, MetricsResolver()));
    long set = 0;
    for (const char* source : {"\\frac{a}{b}", "x", "\\sqrt{x}", "\\binom{n}{k}", "x^2_i",
                               "\\sum_{i}^{n} x", "\\left(\\frac{a}{b}\\right)"}) {
        for (const bool display : {false, true}) {
            CHECK_CONTEXT("%s%s", source, display ? ", display" : "");
            NodeArena arena;
            Formula formula;
            Refusal refusal;
            Node box;
            if (typeset(source, display, fonts, arena, formula, box, refusal)) {
                ++set;
                CHECK(all_dimensions_in_bound(arena, box));
            } else {
                std::printf("    %s%s: %s\n", source, display ? ", display" : "",
                            refusal.reason.c_str());
                CHECK(refusal.kind == RefusalKind::LAYOUT);
                CHECK_EQ(arena.box_count(), size_t(0));
            }
        }
    }
    // Some of them set: that is where a dimension could have been kept.
    CHECK(set >= 2);
}

TEST(h2_through_the_public_api_the_wide_formula_is_its_source_and_every_command_is_in_range)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const std::string text = "before $" + repeated("\\qquad", 25000) + "$ after";
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    ImTypesetStyle style = ImTypeset::GetStyle();
    style.EmSize = 16.0f;
    ImTypeset::SetParagraphText(p, text.c_str(), nullptr, nullptr, 0, ImTypesetTextFlags_Math);
    ImTypeset::LayoutParagraph(p, 400.0f, &style);
    REQUIRE_EQ(ImTypeset::GetParagraphFormulaCount(p), 1);
    const ImTypesetFormula formula = ImTypeset::GetParagraphFormula(p, 0);
    CHECK(formula.Refused);
    CHECK(starts_with(formula.Reason, "the formula is too large to set"));
    CHECK_EQ(ImTypeset::GetParagraphError(p)->Code, ImTypesetError_MathRefused);
    for (const Node& node : p->block.run.nodes) {
        if (node.type == NodeType::BOX) {
            const Box& box = p->arena.box(node);
            CHECK(box.width <= MAX_DIMEN && box.height <= MAX_DIMEN && box.depth <= MAX_DIMEN);
        }
    }
    // The source, set as code, is one word of 150,000 characters on one
    // overfull line: far to the right, and nowhere near the end of a Scaled.
    for (const DrawCmd& cmd : shipped_commands(*p)) {
        CHECK(cmd.x >= -MAX_DIMEN && cmd.x <= SCALED_LIMIT / 4);
        CHECK(cmd.width >= 0 && cmd.width <= SCALED_LIMIT / 4);
    }
    // Asked everything, under the sanitisers.
    {
        Frame frame;
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(500.0f, 500.0f));
        ImGui::Begin("h2", nullptr, ImGuiWindowFlags_NoDecoration);
        const ImTypesetTextRange all = {0, static_cast<int>(text.size())};
        CHECK(ImTypeset::DrawParagraph(p, ImGui::GetWindowDrawList(), ImVec2(10.0f, 10.0f), &all));
        ImGui::End();
    }
    (void)ImTypeset::HitTestParagraph(p, ImVec2(1.0e9f, 5.0f));
    ImGuiTextBuffer copy;
    const ImTypesetTextRange all = {0, static_cast<int>(text.size())};
    ImTypeset::AppendParagraphRangeText(p, all, &copy);
    CHECK_EQ(std::string(copy.c_str()), text);
}

// --- H3 and H8: nothing the library writes depends on the C locale -------------------

TEST(h3_a_layout_refusal_writes_its_proportion_with_a_point_in_every_locale)
{
    math_golden::FakeFonts chained("chained", "none");
    MathFonts fonts;
    REQUIRE(fonts.init(18000, chained.math(), chained.text()));
    Formula formula;
    MathStatus status;
    status.error = MathError::DELIMITER_TOO_SMALL;
    status.codepoint = SURD;
    const struct {
        Scaled needed;
        Scaled largest;
        const char* written;
    } cases[] = {
        {5, 4, "1.25"},       {90901, 90900, "1.00"},
        {3, 2, "1.50"},       {1, 8, "0.12"},
        {3, 8, "0.38"},       {1000001, 4, "250000.25"},
        {2001, 1000, "2.00"}, {MAX_DIMEN, 1, "1073741823.00"},
    };
    const auto check_all = [&](const char* where) {
        for (const auto& c : cases) {
            CHECK_CONTEXT("%s: %lld / %lld", where, static_cast<long long>(c.needed),
                          static_cast<long long>(c.largest));
            status.needed = c.needed;
            status.largest = c.largest;
            CHECK_EQ(refusal_from(status, "", formula, fonts).reason,
                     std::string("what is under the root needs a radical sign ") + c.written +
                         " times as tall as the largest the math face has, and one is not "
                         "assembled from pieces here");
        }
    };
    check_all("the C locale");

    // Against printf itself, in the C locale, where it writes what Python's
    // format does: quotients that are exact halves of a hundredth, ones a
    // rounding error away from a half, and whatever a fixed generator makes.
    {
        uint64_t state = 0x2545F4914F6CDD1Dull;
        const auto next = [&state]() {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            return state >> 20;
        };
        long compared = 0;
        const auto compare = [&](Scaled needed, Scaled largest) {
            if (largest <= 0)
                return;
            status.needed = needed;
            status.largest = largest;
            char expected[400];
            std::snprintf(expected, sizeof expected,
                          "what is under the root needs a radical sign %.2f times as tall as the "
                          "largest the math face has, and one is not assembled from pieces here",
                          static_cast<double>(needed) / static_cast<double>(largest));
            const std::string reason = refusal_from(status, "", formula, fonts).reason;
            if (reason != expected) {
                CHECK_CONTEXT("%lld / %lld", static_cast<long long>(needed),
                              static_cast<long long>(largest));
                CHECK_EQ(reason, std::string(expected));
            }
            ++compared;
        };
        for (Scaled k = 1; k < 4000; k += 2) {
            compare(k, 200); // k/200: a half of a hundredth, exactly
            compare(k * 5, 1000);
            compare(k * 1000 + 1, 200000);
            compare(k * 1000 - 1, 200000);
            compare(k, 8);
            compare(k, 1024);
            compare(k, 1 << 20);
        }
        for (int i = 0; i < 300000; ++i) {
            const int needed_bits = 1 + static_cast<int>(next() % 40);
            const int largest_bits = 1 + static_cast<int>(next() % 31);
            compare(static_cast<Scaled>(next() & ((uint64_t(1) << needed_bits) - 1)),
                    static_cast<Scaled>(next() & ((uint64_t(1) << largest_bits) - 1)));
        }
        compare(SCALED_MAX, 1);
        compare(SCALED_MAX, 3);
        compare(SCALED_MAX, MAX_DIMEN);
        compare(0, 5);
        compare(1, SCALED_MAX);
        compare(995, 1000);
        compare(9995, 10000);
        compare(99999, 100000);
        CHECK(compared > 300000);
    }
    DecimalComma comma;
    if (comma.name == nullptr) {
        std::printf(
            "    SKIPPED under a decimal comma: this machine has none of the locales "
            "tried (de_DE, fr_FR, nl_NL, es_ES, it_IT, ru_RU). Compile one with "
            "`localedef -i de_DE -f UTF-8 <dir>/de_DE.UTF-8` and run with LOCPATH=<dir>.\n");
        return;
    }
    std::printf("    LC_NUMERIC = %s\n", comma.name);
    check_all(comma.name);
}

TEST(h8_a_paragraph_is_laid_out_the_same_under_a_decimal_comma)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    const char* const text =
        "The radius $r = \\frac{a}{2}$ and a root $\\sqrt{x^2 + 1}$, set in a paragraph long "
        "enough to be broken into several lines at the width it is given, twice.";
    const auto lay_out = [&](std::vector<DrawCmd>& cmds, ImVec2& size) {
        ImTypesetParagraph* p = ImTypeset::CreateParagraph();
        REQUIRE(p != nullptr);
        ImTypesetStyle style = ImTypeset::GetStyle();
        // Decimals a host writes, which the library reads back as decimals.
        style.LineHeight = 1.35f;
        style.EmSize = 15.5f;
        ImTypesetRun run;
        run.Start = 4;
        run.End = 10;
        run.Scale = 1.2f;
        CHECK(ImTypeset::SetParagraphText(p, text, nullptr, &run, 1, ImTypesetTextFlags_Math));
        CHECK(ImTypeset::LayoutParagraph(p, 260.5f, &style));
        cmds = shipped_commands(*p);
        size = ImTypeset::GetParagraphSize(p);
        ImTypeset::DestroyParagraph(p);
    };
    std::vector<DrawCmd> plain;
    ImVec2 plain_size;
    lay_out(plain, plain_size);
    REQUIRE(plain.size() > 100);

    DecimalComma comma;
    if (comma.name == nullptr) {
        std::printf("    SKIPPED: no locale with a decimal comma on this machine (see h3)\n");
        return;
    }
    std::vector<DrawCmd> other;
    ImVec2 other_size;
    lay_out(other, other_size);
    REQUIRE_EQ(other.size(), plain.size());
    size_t different = 0;
    for (size_t i = 0; i < plain.size(); ++i)
        different += std::memcmp(&plain[i], &other[i], sizeof(DrawCmd)) != 0 ? 1u : 0u;
    CHECK_EQ(different, size_t(0));
    CHECK_EQ(other_size.x, plain_size.x);
    CHECK_EQ(other_size.y, plain_size.y);
}

// --- H4: a reason is a C string ------------------------------------------------------

TEST(h4_no_reason_carries_a_nul_or_ends_early_in_the_public_structs)
{
    // Sources with a NUL wherever the parser quotes what it read: a control
    // symbol, a character outside the subset, inside \text, after \left.
    const std::string sources[] = {
        std::string("\\\0", 2),
        std::string("\0", 1),
        std::string("a\0b", 3),
        std::string("\\text{a\0}", 9),
        std::string("\\left\0", 6),
        std::string("\\mathbb{\0}", 10),
        std::string("\\frac\0", 6),
        std::string("x^\0", 3),
        std::string("\\operatorname{\0}", 16),
    };
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    int refused = 0;
    for (const std::string& source : sources) {
        CHECK_CONTEXT("source of %zu bytes", source.size());
        Formula formula;
        Refusal refusal;
        if (parse(source, false, formula, refusal))
            continue;
        ++refused;
        const std::string shown = c_string_reason(refusal.reason);
        CHECK(shown.find('\0') == std::string::npos);
        // Nothing but the NULs is touched.
        if (refusal.reason.find('\0') == std::string::npos)
            CHECK_EQ(shown, refusal.reason);

        const std::string text = "see $" + source + "$ here";
        ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size(), nullptr, 0,
                                    ImTypesetTextFlags_Math);
        ImTypeset::LayoutParagraph(p, 300.0f, nullptr);
        REQUIRE_EQ(ImTypeset::GetParagraphFormulaCount(p), 1);
        const ImTypesetFormula formula_info = ImTypeset::GetParagraphFormula(p, 0);
        CHECK(formula_info.Refused);
        // What a `const char*` consumer reads is the whole reason.
        CHECK_EQ(std::string(formula_info.Reason), shown);
        CHECK_EQ(std::string(ImTypeset::GetParagraphError(p)->Message), shown);
        CHECK_EQ(std::string(ImTypeset::GetLastError()->Message), shown);
    }
    CHECK(refused >= 5);
    // The spelling is Python's for the character, as elsewhere in a reason.
    CHECK_EQ(c_string_reason(std::string("a\0b", 3)), std::string("a\\x00b"));
}

// --- H5: the selection model on a buffer nobody shipped ------------------------------

namespace
{

// Two lines of three glyphs each, with whatever coordinates are given.
std::vector<DrawCmd> two_lines(Scaled x, Scaled y, Scaled width, Scaled height, Scaled depth)
{
    std::vector<DrawCmd> cmds;
    const FontKey key{1};
    for (uint32_t line = 0; line < 2; ++line) {
        DrawCmd opened;
        opened.type = CmdType::LINE;
        opened.x = x;
        opened.y = y;
        opened.width = width;
        opened.height = height;
        opened.depth = depth;
        opened.index = line * 3;
        opened.end = line * 3 + 3;
        cmds.push_back(opened);
        for (uint32_t i = 0; i < 3; ++i)
            cmds.push_back(
                DrawCmd::glyph(key, 'a' + line * 3 + i, i % 2 == 0 ? x : width, y, line * 3 + i));
    }
    return cmds;
}

} // namespace

TEST(h5_a_buffer_with_coordinates_at_the_ends_of_the_number_line_is_indexed_and_answered)
{
    const Scaled most = std::numeric_limits<Scaled>::max();
    const Scaled least = std::numeric_limits<Scaled>::min();
    const Scaled values[] = {most, least, 0, 1, -1, most - 1, least + 1, SCALED_LIMIT, MAX_DIMEN};
    const Point points[] = {{0, 0},        {most, most}, {least, least}, {most, least},
                            {least, most}, {1, -1},      {MAX_DIMEN, 0}, {0, most}};
    long models = 0;
    for (const Scaled x : values) {
        for (const Scaled y : values) {
            for (const Scaled width : {most, least, Scaled(10)}) {
                for (const Scaled height : {most, least, Scaled(5)}) {
                    const std::vector<DrawCmd> cmds = two_lines(x, y, width, height, y);
                    SelectionModel model;
                    model.build(cmds);
                    ++models;
                    CHECK_EQ(model.length(), 6u);
                    for (const Point& point : points) {
                        CHECK(model.hit(point) <= 6u);
                        (void)model.fragment_at(point);
                        (void)model.select_at(point, points[0], Granularity::WORD);
                        (void)model.select_at(points[1], point, Granularity::LINE);
                    }
                    std::vector<Rect> rects;
                    model.rects(Selection{0, 6}, rects);
                    std::string text;
                    model.text(Selection{0, 6}, TextData(), text);
                    CHECK_EQ(text, std::string("abc def"));
                }
            }
        }
    }
    CHECK_EQ(models, 9L * 9 * 3 * 3);
}

TEST(h5_text_data_given_out_of_order_copies_as_it_does_in_order)
{
    // Three lines; an atom on the second; a separator before the second and
    // the third; an original for two glyphs.
    std::vector<DrawCmd> cmds;
    const FontKey key{1};
    uint32_t index = 0;
    for (uint32_t line = 0; line < 3; ++line) {
        DrawCmd opened;
        opened.type = CmdType::LINE;
        opened.y = static_cast<Scaled>(line) * 20;
        opened.width = 100;
        opened.height = 10;
        opened.depth = 2;
        opened.index = index;
        opened.end = index + 4;
        cmds.push_back(opened);
        for (uint32_t i = 0; i < 4; ++i, ++index) {
            if (line == 1 && i == 1) {
                DrawCmd atom = DrawCmd::set_glue(i * 10, opened.y, 10, 10, 10, 2, index);
                atom.type = CmdType::ATOM;
                cmds.push_back(atom);
            } else {
                cmds.push_back(DrawCmd::glyph(key, 'a' + index, i * 10, opened.y, index));
            }
        }
    }
    SelectionModel model;
    model.build(cmds);
    REQUIRE_EQ(model.length(), 12u);

    const uint32_t breaks[] = {4, 8};
    const IndexedText separators[] = {{4, "\n\n", 2}, {8, "\t", 1}};
    const IndexedText atoms[] = {{5, "$x$", 3}, {7, "unused", 6}};
    const IndexedText originals[] = {{0, "A", 1}, {2, "", 0}, {9, "J", 1}, {11, "L", 1}};
    TextData sorted;
    sorted.hard_breaks = breaks;
    sorted.hard_break_count = 2;
    sorted.separators = separators;
    sorted.separator_count = 2;
    sorted.atoms = atoms;
    sorted.atom_count = 2;
    sorted.originals = originals;
    sorted.original_count = 4;
    std::string expected;
    model.text(Selection{0, 12}, sorted, expected);
    CHECK_EQ(expected, std::string("Abd\n\ne$x$gh\tiJkL"));

    const uint32_t breaks_r[] = {8, 4};
    const IndexedText separators_r[] = {{8, "\t", 1}, {4, "\n\n", 2}};
    const IndexedText atoms_r[] = {{7, "unused", 6}, {5, "$x$", 3}};
    const IndexedText originals_r[] = {{11, "L", 1}, {9, "J", 1}, {0, "A", 1}, {2, "", 0}};
    TextData shuffled;
    shuffled.hard_breaks = breaks_r;
    shuffled.hard_break_count = 2;
    shuffled.separators = separators_r;
    shuffled.separator_count = 2;
    shuffled.atoms = atoms_r;
    shuffled.atom_count = 2;
    shuffled.originals = originals_r;
    shuffled.original_count = 4;
    std::string out_of_order;
    model.text(Selection{0, 12}, shuffled, out_of_order);
    CHECK_EQ(out_of_order, expected);

    // Hard breaks alone, which are searched another way.
    TextData only_breaks;
    only_breaks.hard_breaks = breaks;
    only_breaks.hard_break_count = 2;
    std::string by_breaks;
    model.text(Selection{0, 12}, only_breaks, by_breaks);
    TextData only_breaks_r;
    only_breaks_r.hard_breaks = breaks_r;
    only_breaks_r.hard_break_count = 2;
    std::string by_breaks_r;
    model.text(Selection{0, 12}, only_breaks_r, by_breaks_r);
    CHECK_EQ(by_breaks, std::string("abcd\nexgh\nijkl").replace(6, 1, ""));
    CHECK_EQ(by_breaks_r, by_breaks);
}

TEST(h5_what_the_public_api_hands_the_selection_model_is_in_order)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    // Hard breaks, formulas on several lines, and characters no face here
    // has, which are drawn as stand-ins and copy as what was written.
    const std::string text = "one $a+b$ two\nthree \xE4\xB8\xAD $c$\n$d$ four \xE6\x96\x87 five "
                             "$e^2$ six\nseven $\\frac{1}{2}$ \xE5\xAD\x97 eight";
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    ImTypeset::SetParagraphText(p, text.data(), text.data() + text.size(), nullptr, 0,
                                ImTypesetTextFlags_Math | ImTypesetTextFlags_HardBreaks);
    for (const float width : {60.0f, 150.0f, 2000.0f}) {
        CHECK_CONTEXT("width %g", static_cast<double>(width));
        ImTypeset::LayoutParagraph(p, width, nullptr);
        ImGuiTextBuffer copy;
        const ImTypesetTextRange all = {0, static_cast<int>(text.size())};
        ImTypeset::AppendParagraphRangeText(p, all, &copy);
        CHECK_EQ(std::string(copy.c_str()), text);
        const auto ascending = [](const std::vector<IndexedText>& entries) {
            for (size_t i = 1; i < entries.size(); ++i) {
                if (entries[i].index <= entries[i - 1].index)
                    return false;
            }
            return true;
        };
        CHECK(p->separators.size() >= 3);
        CHECK(p->atom_texts.size() == 5);
        CHECK(p->originals.size() >= 3);
        CHECK(ascending(p->separators));
        CHECK(ascending(p->atom_texts));
        CHECK(ascending(p->originals));
    }
}

// --- The draw loop -------------------------------------------------------------------

namespace
{

// A quad as the draw loop writes one: four vertices, the first at the top
// left and the third at the bottom right.
struct Quad {
    float x0, y0, x1, y1;
    ImVec2 uv0, uv1;
    ImU32 colour;
    bool white() const { return uv0.x == uv1.x && uv0.y == uv1.y; }
};

std::vector<Quad> quads_of(const ImDrawList& list)
{
    std::vector<Quad> out;
    for (int i = 0; i + 3 < list.VtxBuffer.Size; i += 4) {
        const ImDrawVert& a = list.VtxBuffer[i];
        const ImDrawVert& c = list.VtxBuffer[i + 2];
        out.push_back(Quad{a.pos.x, a.pos.y, c.pos.x, c.pos.y, a.uv, c.uv, a.col});
    }
    return out;
}

// A draw list that belongs to no window, on shared data of its own.
struct OwnList {
    explicit OwnList(ImVec2 white)
        : list(&shared)
    {
        shared.TexUvWhitePixel = white;
        list._ResetForNewFrame();
        list.PushClipRect(ImVec2(-1.0e6f, -1.0e6f), ImVec2(1.0e6f, 1.0e6f));
    }
    ImDrawListSharedData shared;
    ImDrawList list;
};

DrawCmd line_cmd(Scaled x, Scaled y, Scaled width, Scaled height, Scaled depth)
{
    DrawCmd line;
    line.type = CmdType::LINE;
    line.x = x;
    line.y = y;
    line.width = width;
    line.height = height;
    line.depth = depth;
    return line;
}

} // namespace

TEST(d3_a_rectangle_takes_the_white_texel_of_the_list_it_is_written_to)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    const ImVec2 context_white = ImGui::GetFontTexUvWhitePixel();
    const ImVec2 own_white(0.25f, 0.75f);
    REQUIRE(context_white.x != own_white.x || context_white.y != own_white.y);
    OwnList own(own_white);

    // A fill, a highlight, and a line with the overlay: a frame and a
    // baseline. All of them rectangles, none of them text.
    const DrawCmd cmds[] = {line_cmd(10 * UNITY, 30 * UNITY, 100 * UNITY, 10 * UNITY, 3 * UNITY)};
    const Fill fills[] = {{Rect{0, 0, 20 * UNITY, 20 * UNITY}, IM_COL32(1, 2, 3, 255)}};
    const Rect highlights[] = {Rect{5 * UNITY, 5 * UNITY, 9 * UNITY, 9 * UNITY}};
    DrawRun run;
    run.cmds = cmds;
    run.count = 1;
    DrawArgs args;
    args.fills = fills;
    args.fill_count = 1;
    args.highlights = highlights;
    args.highlight_count = 1;
    args.debug = true;
    Drawer drawer;
    drawer.draw_runs(&own.list, &run, 1, args, FaceFonts(), MetricsResolver());
    const std::vector<Quad> quads = quads_of(own.list);
    // Fill, highlight, four sides of the line's box, its baseline.
    REQUIRE_EQ(quads.size(), size_t(7));
    for (const Quad& quad : quads) {
        CHECK_EQ(quad.uv0.x, own_white.x);
        CHECK_EQ(quad.uv0.y, own_white.y);
        CHECK(quad.white());
    }
    CHECK_EQ(own.list.IdxBuffer.Size, 42);
    // The overlay's geometry, which is this library's: the frame of the line
    // box one pixel thick, inside it, and the pixel row under the baseline.
    const auto px = [](Scaled points) {
        return static_cast<float>(
            snap_px(device_coordinate(0.0f, points * UNITY, DEFAULT_PX_PER_INCH)));
    };
    const float left = px(10);
    const float right = px(110);
    const float top = px(20);
    const float bottom = px(33);
    const float baseline = px(30);
    REQUIRE(right - left > 2.0f && bottom - top > 2.0f);
    const float expected[5][4] = {
        {left, top, right, top + 1.0f},
        {left, bottom - 1.0f, right, bottom},
        {left, top + 1.0f, left + 1.0f, bottom - 1.0f},
        {right - 1.0f, top + 1.0f, right, bottom - 1.0f},
        {left, baseline, right, baseline + 1.0f},
    };
    for (int i = 0; i < 5; ++i) {
        CHECK_CONTEXT("overlay rectangle %d", i);
        const Quad& quad = quads[static_cast<size_t>(2 + i)];
        CHECK_EQ(quad.x0, expected[i][0]);
        CHECK_EQ(quad.y0, expected[i][1]);
        CHECK_EQ(quad.x1, expected[i][2]);
        CHECK_EQ(quad.y1, expected[i][3]);
    }

    // The same into buffers: the same seven, with the context's white texel.
    DrawBuffers buffers;
    drawer.draw_runs(buffers, ImVec4(-1.0e6f, -1.0e6f, 1.0e6f, 1.0e6f), &run, 1, args, FaceFonts(),
                     MetricsResolver());
    REQUIRE_EQ(buffers.vertices.Size, 28);
    for (int i = 0; i < 28; ++i) {
        CHECK_EQ(buffers.vertices[i].pos.x, own.list.VtxBuffer[i].pos.x);
        CHECK_EQ(buffers.vertices[i].pos.y, own.list.VtxBuffer[i].pos.y);
        CHECK_EQ(buffers.vertices[i].uv.x, context_white.x);
        CHECK_EQ(buffers.vertices[i].uv.y, context_white.y);
    }
}

TEST(d4_past_64k_vertices_the_triangles_are_those_of_a_reservation_per_quad)
{
    if (sizeof(ImDrawIdx) != 2) {
        std::printf("    SKIPPED: ImDrawIdx is %zu bytes here, and a list is then one command\n",
                    sizeof(ImDrawIdx));
        return;
    }
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    Frame frame;
    // 40,000 rectangles: 160,000 vertices, so two changes of vertex offset.
    std::vector<Fill> fills;
    for (int i = 0; i < 40000; ++i) {
        const Scaled x = static_cast<Scaled>(i % 200) * 3 * UNITY;
        const Scaled y = static_cast<Scaled>(i / 200) * 3 * UNITY;
        fills.push_back(Fill{Rect{x, y, 2 * UNITY, 2 * UNITY}, IM_COL32(i & 255, 7, 9, 255)});
    }
    DrawArgs args;
    args.fills = fills.data();
    args.fill_count = fills.size();
    const ImVec2 white(0.5f, 0.5f);
    OwnList ours(white);
    ours.list.Flags |= imgui_compat::AllowVtxOffset;
    Drawer drawer;
    drawer.draw_runs(&ours.list, nullptr, 0, args, FaceFonts(), MetricsResolver());

    // What a reservation for each rectangle makes, which is what the
    // reference's one call per rectangle makes.
    OwnList theirs(white);
    theirs.list.Flags |= imgui_compat::AllowVtxOffset;
    for (const DrawOp& op : drawer.ops()) {
        theirs.list.PrimReserve(6, 4);
        theirs.list.PrimRect(ImVec2(op.x0, op.y0), ImVec2(op.x1, op.y1), op.colour);
    }

    REQUIRE_EQ(ours.list.VtxBuffer.Size, 160000);
    REQUIRE_EQ(ours.list.VtxBuffer.Size, theirs.list.VtxBuffer.Size);
    REQUIRE_EQ(ours.list.IdxBuffer.Size, theirs.list.IdxBuffer.Size);
    CHECK(std::memcmp(ours.list.VtxBuffer.Data, theirs.list.VtxBuffer.Data,
                      static_cast<size_t>(ours.list.VtxBuffer.Size) * sizeof(ImDrawVert)) == 0);
    // Every index, as the vertex it names once its command's offset is added.
    const auto resolved = [](const ImDrawList& list) {
        std::vector<uint32_t> out;
        for (const ImDrawCmd& cmd : list.CmdBuffer) {
            for (unsigned int i = 0; i < cmd.ElemCount; ++i)
                out.push_back(cmd.VtxOffset + list.IdxBuffer[static_cast<int>(cmd.IdxOffset + i)]);
        }
        return out;
    };
    const std::vector<uint32_t> our_triangles = resolved(ours.list);
    const std::vector<uint32_t> their_triangles = resolved(theirs.list);
    REQUIRE_EQ(our_triangles.size(), size_t(240000));
    CHECK(our_triangles == their_triangles);
    // Where the commands divide is the one thing that differs: each of ours
    // ends at most a batch of quads before theirs. (No command holds an index
    // past 16 bits either way, or the triangles above would not agree.)
    REQUIRE_EQ(ours.list.CmdBuffer.Size, theirs.list.CmdBuffer.Size);
    REQUIRE_EQ(ours.list.CmdBuffer.Size, 3);
    long earliest = 0;
    for (int i = 0; i + 1 < ours.list.CmdBuffer.Size; ++i) {
        const long ours_end =
            static_cast<long>(ours.list.CmdBuffer[i].IdxOffset + ours.list.CmdBuffer[i].ElemCount);
        const long theirs_end = static_cast<long>(theirs.list.CmdBuffer[i].IdxOffset +
                                                  theirs.list.CmdBuffer[i].ElemCount);
        CHECK(ours_end <= theirs_end);
        CHECK(theirs_end - ours_end <= 1024 * 6);
        earliest = std::max(earliest, theirs_end - ours_end);
    }
    std::printf("    a draw command ends up to %ld quads before a per-quad reservation's\n",
                earliest / 6);
}

TEST(d6_an_atlas_that_fills_and_is_repacked_during_a_draw_leaves_every_quad_on_its_glyph)
{
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    // Room for a few sizes of the text below and no more, so that the atlas
    // grows to this and then has to throw bakes away to go on.
    atlas->TexMaxWidth = 512;
    atlas->TexMaxHeight = 1024;
    std::string text;
    for (char c = '!'; c <= '~'; ++c) {
        if (c == '$' || c == '\\')
            continue;
        text += c;
        if (c % 8 == 0)
            text += ' ';
    }
    const int bold_from = static_cast<int>(text.size()) / 2;
    ImTypesetRun run;
    run.Start = bold_from;
    run.End = static_cast<int>(text.size());
    run.Face = ImTypesetFace_Bold;
    run.Scale = 1.25f;
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    REQUIRE(ImTypeset::SetParagraphText(p, text.c_str(), nullptr, &run, 1, 0));

    int changed_during_a_draw = 0;
    int discarded = 0;
    std::vector<ImGuiID> alive_before;
    long quads_checked = 0;
    for (int frame_number = 0; frame_number < 120; ++frame_number) {
        CHECK_CONTEXT("frame %d", frame_number);
        ImTypesetStyle style = ImTypeset::GetStyle();
        // Another size every frame, out of 27, so that each comes round again
        // after its bake has had time to be thrown away. None so large that
        // the paragraph leaves the display, where its quads would be clipped.
        style.EmSize = 14.0f + static_cast<float>(frame_number * 7 % 27);
        Frame frame;
        REQUIRE(ImTypeset::LayoutParagraph(p, 700.0f, &style));
        ImDrawList* list = ImGui::GetBackgroundDrawList();
        const int first_vertex = list->VtxBuffer.Size;
        const ImTextureData* const texture_before = atlas->TexData;
        const int width_before = texture_before->Width;
        const int height_before = texture_before->Height;
        CHECK(ImTypeset::DrawParagraph(p, list, ImVec2(4.0f, 4.0f), nullptr));
        if (atlas->TexData != texture_before || atlas->TexData->Width != width_before ||
            atlas->TexData->Height != height_before)
            ++changed_during_a_draw;

        // Every glyph in every bake that is alive, by its texture rectangle.
        std::vector<ImVec4> rects;
        std::vector<ImGuiID> alive;
        for (int i = 0; i < atlas->Builder->BakedPool.Size; ++i) {
            const ImFontBaked& baked = atlas->Builder->BakedPool[i];
            if (baked.WantDestroy)
                continue;
            alive.push_back(baked.BakedId);
            for (const ImFontGlyph& glyph : baked.Glyphs) {
                if (glyph.Visible)
                    rects.push_back(ImVec4(glyph.U0, glyph.V0, glyph.U1, glyph.V1));
            }
        }
        std::sort(alive.begin(), alive.end());
        for (const ImGuiID id : alive_before)
            discarded += std::binary_search(alive.begin(), alive.end(), id) ? 0 : 1;
        alive_before = alive;
        const auto by_rect = [](const ImVec4& a, const ImVec4& b) {
            return std::memcmp(&a, &b, sizeof a) < 0;
        };
        std::sort(rects.begin(), rects.end(), by_rect);
        int glyph_quads = 0;
        int strays = 0;
        for (int i = first_vertex; i + 3 < list->VtxBuffer.Size; i += 4) {
            const ImDrawVert& a = list->VtxBuffer[i];
            const ImDrawVert& c = list->VtxBuffer[i + 2];
            ++glyph_quads;
            const ImVec4 wanted(a.uv.x, a.uv.y, c.uv.x, c.uv.y);
            if (!std::binary_search(rects.begin(), rects.end(), wanted, by_rect))
                ++strays;
        }
        // Every character of the text but its spaces came to a quad, and each
        // quad shows a glyph that is in the atlas as it now stands.
        CHECK_EQ(glyph_quads,
                 static_cast<int>(text.size() - std::count(text.begin(), text.end(), ' ')));
        CHECK_EQ(strays, 0);
        quads_checked += glyph_quads;
    }
    std::printf("    120 frames, %ld quads; the atlas is %d x %d; it changed under a draw in %d "
                "frames; %d bakes were discarded in all\n",
                quads_checked, atlas->TexData->Width, atlas->TexData->Height, changed_during_a_draw,
                discarded);
    // Without these the run shows nothing: the atlas has to have reached its
    // limit and thrown bakes away while a draw was loading glyphs.
    CHECK(atlas->TexData->Width <= 512 && atlas->TexData->Height <= 1024);
    CHECK(changed_during_a_draw >= 5);
    CHECK(discarded >= 20);
    ImTypeset::DestroyParagraph(p);
}

// --- D7: a radical sign and its bar, late in a long formula --------------------------

TEST(d7_a_radical_sign_meets_its_bar_however_far_along_a_formula_it_stands)
{
    // Under the reference's rules an inline formula is one rigid span, its
    // glyphs placed by adding rounded gaps from the first one, and the rule
    // over a radicand is snapped on its own: over these same 576 draws the
    // bar starts from 3 px inside the sign's quad to 3 px clear of it, the
    // further along the further off. Under the rules the public API draws
    // with (DrawRules::product) the sign and the bar's end are each put
    // within half a pixel of their place in the formula, so the joint is
    // within a pixel of what it is with nothing before the sign, and the
    // same wherever the page is put. Measured over sizes, lengths and
    // sub-pixel origins, at the two joints: the bar's left edge against the
    // sign's right edge, and their tops.
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    float widest_gap = -1.0e9f, widest_overlap = 1.0e9f;
    float highest = -1.0e9f, lowest = 1.0e9f;
    float at_once_gap = 0.0f, at_once_top = 0.0f;
    long measured = 0;
    int formulas = 0, apart = 0;
    for (const float em : {11.0f, 13.0f, 16.0f, 19.0f, 24.0f, 32.0f}) {
        for (const int terms : {0, 1, 5, 13, 26, 60}) {
            std::string text = "$";
            for (int i = 0; i < terms; ++i) {
                text += static_cast<char>('a' + i % 26);
                text += '+';
            }
            text += "\\sqrt{x}$";
            ImTypesetStyle style = ImTypeset::GetStyle();
            style.EmSize = em;
            REQUIRE(ImTypeset::SetParagraphText(p, text.c_str(), nullptr, nullptr, 0,
                                                ImTypesetTextFlags_Math));
            REQUIRE(ImTypeset::LayoutParagraph(p, 100000.0f, &style));
            REQUIRE_EQ(ImTypeset::GetParagraphFormulaCount(p), 1);
            REQUIRE(!ImTypeset::GetParagraphFormula(p, 0).Refused);
            float gap_low = 1.0e9f, gap_high = -1.0e9f, top_low = 1.0e9f, top_high = -1.0e9f;
            for (int step = 0; step < 16; ++step) {
                Frame frame;
                ImDrawList* list = ImGui::GetBackgroundDrawList();
                list->PushClipRect(ImVec2(-1.0e6f, -1.0e6f), ImVec2(1.0e6f, 1.0e6f));
                const int first = list->VtxBuffer.Size;
                const ImVec2 origin(3.0f + static_cast<float>(step) / 16.0f,
                                    40.0f + static_cast<float>(step * 5 % 16) / 16.0f);
                REQUIRE(ImTypeset::DrawParagraph(p, list, origin, nullptr));
                list->PopClipRect();
                // The bar is the one rectangle; the sign is the glyph whose
                // top right corner is nearest the bar's top left.
                const Quad* bar = nullptr;
                std::vector<Quad> quads;
                for (int i = first; i + 3 < list->VtxBuffer.Size; i += 4) {
                    const ImDrawVert& a = list->VtxBuffer[i];
                    const ImDrawVert& c = list->VtxBuffer[i + 2];
                    quads.push_back(Quad{a.pos.x, a.pos.y, c.pos.x, c.pos.y, a.uv, c.uv, a.col});
                }
                int bars = 0;
                for (const Quad& quad : quads) {
                    if (quad.white()) {
                        bar = &quad;
                        ++bars;
                    }
                }
                REQUIRE_EQ(bars, 1);
                const Quad* sign = nullptr;
                float nearest = 1.0e9f;
                for (const Quad& quad : quads) {
                    if (quad.white())
                        continue;
                    const float distance =
                        std::fabs(quad.x1 - bar->x0) + std::fabs(quad.y0 - bar->y0);
                    if (distance < nearest) {
                        nearest = distance;
                        sign = &quad;
                    }
                }
                REQUIRE(sign != nullptr);
                const float gap = bar->x0 - sign->x1; // positive: a gap; negative: overlap
                const float rise = bar->y0 - sign->y0;
                gap_low = std::min(gap_low, gap);
                gap_high = std::max(gap_high, gap);
                top_low = std::min(top_low, rise);
                top_high = std::max(top_high, rise);
                ++measured;
            }
            if (terms == 0) {
                at_once_gap = gap_low;
                at_once_top = top_low;
            }
            // Across and down alike: the same wherever the page is put, and
            // within a pixel of what it is with no glyph before the sign.
            // Each is rounded once from the formula's own edge or baseline
            // and nothing adds up.
            CHECK_CONTEXT("em %g, %d terms before the root", static_cast<double>(em), terms);
            CHECK_EQ(gap_low, gap_high);
            CHECK(std::fabs(gap_low - at_once_gap) <= 1.0f);
            CHECK_EQ(top_low, top_high);
            CHECK(std::fabs(top_low - at_once_top) <= 1.0f);
            // And it is a joint: the bar starts at or inside the sign's quad.
            CHECK(gap_high <= 0.0f);
            apart += gap_low != at_once_gap ? 1 : 0;
            ++formulas;
            widest_gap = std::max(widest_gap, gap_high);
            widest_overlap = std::min(widest_overlap, gap_low);
            highest = std::max(highest, top_high);
            lowest = std::min(lowest, top_low);
        }
    }
    std::printf("    over %ld draws: the bar starts %+.0f to %+.0f px from the sign's right edge "
                "and its top is %+.0f to %+.0f px from the sign's; in %d of %d formulas the "
                "joint is a pixel from what it is with nothing before the sign\n",
                measured, static_cast<double>(widest_overlap), static_cast<double>(widest_gap),
                static_cast<double>(lowest), static_cast<double>(highest), apart, formulas);
    ImTypeset::DestroyParagraph(p);
}

namespace
{

// A binder that asks ImGui nothing: every key is bound, every glyph is there,
// and every key is baked at 16 px with an ascent of 12.
class FixedBinder : public FontBinder
{
public:
    void begin() override {}
    Binding bind(uint32_t, FontKey) override { return Binding::BOUND; }
    bool has_glyph(uint32_t, uint32_t, uint32_t) override { return true; }
    bool bake(uint32_t, FontKey, float* size, float* ascent) override
    {
        *size = 16.0f;
        *ascent = 12.0f;
        return true;
    }
};

} // namespace

TEST(d7_a_formula_is_placed_glyph_by_glyph_by_the_reference_and_whole_here)
{
    // One TeX point to the pixel, so a coordinate below reads as pixels.
    const auto sp = [](double pixels) { return static_cast<Scaled>(pixels * 65536.0); };
    const FontKey key{1};
    // A formula whose box starts at 0: four glyphs 10.4 px apart, then a
    // rule, then a glyph under it. The baseline is at 100.3 and the cell 12
    // above it, so the first cell's top is floor(88.3 + 0.5) = 88 and the
    // span's baseline 88 + 12 = 100. The atom's index is on every glyph.
    const uint32_t formula = 7;
    std::vector<DrawCmd> cmds;
    cmds.push_back(line_cmd(0, sp(100.3), sp(200.0), sp(20.0), sp(5.0)));
    cmds.push_back(DrawCmd::atom(0, sp(100.3), sp(70.0), sp(70.0), sp(15.0), sp(3.0), formula));
    for (uint32_t i = 0; i < 4; ++i)
        cmds.push_back(DrawCmd::glyph(key, 'a' + i, sp(10.4 * i), sp(100.3), formula));
    cmds.push_back(DrawCmd::rule_rect(sp(41.6), sp(85.6), sp(20.0), sp(0.9)));
    cmds.push_back(DrawCmd::glyph(key, 'x', sp(45.0), sp(100.3), formula));
    // A glyph of the text straight after the formula, with no glue between.
    cmds.push_back(DrawCmd::glyph(key, ',', sp(55.4), sp(100.3), formula + 1));
    DrawRun run;
    run.cmds = cmds.data();
    run.count = cmds.size();
    DrawArgs args;
    args.px_per_inch = PT_PER_INCH;
    FixedBinder binder;
    Drawer drawer;

    // The reference: every gap of 10.4 rounds down to 10, so the pens are at
    // 0, 10, 20, 30 where the exact places are 0, 10.4, 20.8, 31.2; the rule
    // on its own, by its own corners; the glyph after it the anchor of a new
    // span, and the comma placed from that.
    //   x: floor(41.6 + 0.5) = 42 to floor(61.6 + 0.5) = 62
    //   y: floor(85.6 + 0.5) = 86 to floor(86.5 + 0.5) = 87
    //   the glyph: floor(45.0 + 0.5) = 45; the comma: 45 + floor(10.4 + 0.5) = 55
    args.rules = DrawRules::reference();
    drawer.place(&run, 1, args, binder);
    REQUIRE_EQ(drawer.ops().size(), size_t(7));
    for (size_t i = 0; i < 4; ++i)
        CHECK_EQ(drawer.ops()[i].x0, static_cast<float>(10 * i));
    CHECK(drawer.ops()[4].type == OpType::RECT_FILLED);
    CHECK_EQ(drawer.ops()[4].x0, 42.0f);
    CHECK_EQ(drawer.ops()[4].x1, 62.0f);
    CHECK_EQ(drawer.ops()[4].y0, 86.0f);
    CHECK_EQ(drawer.ops()[4].y1, 87.0f);
    CHECK_EQ(drawer.ops()[5].x0, 45.0f);
    CHECK_EQ(drawer.ops()[5].y0, 88.0f);
    CHECK_EQ(drawer.ops()[6].x0, 55.0f);

    // The product: everything of the formula from its box's left edge, which
    // is put at floor(0 + 0.5) = 0.
    //   the glyphs: floor(10.4 + 0.5) = 10, floor(20.8 + 0.5) = 21, floor(31.2 + 0.5) = 31
    //   the rule, x: floor(41.6 + 0.5) = 42 to floor(61.6 + 0.5) = 62
    //   y, from the span's baseline at 100:
    //      floor(100 + (85.6 - 100.3) + 0.5) = 85 to floor(100 + (86.5 - 100.3) + 0.5) = 86
    //   the glyph after it: floor(45.0 + 0.5) = 45, on the row of the others
    //   the comma is text: from the glyph before it, 45 + floor(10.4 + 0.5) = 55
    args.rules = DrawRules::product();
    drawer.place(&run, 1, args, binder);
    REQUIRE_EQ(drawer.ops().size(), size_t(7));
    CHECK_EQ(drawer.ops()[0].x0, 0.0f);
    CHECK_EQ(drawer.ops()[1].x0, 10.0f);
    CHECK_EQ(drawer.ops()[2].x0, 21.0f);
    CHECK_EQ(drawer.ops()[3].x0, 31.0f);
    CHECK(drawer.ops()[4].type == OpType::RECT_FILLED);
    CHECK_EQ(drawer.ops()[4].x0, 42.0f);
    CHECK_EQ(drawer.ops()[4].x1, 62.0f);
    CHECK_EQ(drawer.ops()[4].y0, 85.0f);
    CHECK_EQ(drawer.ops()[4].y1, 86.0f);
    CHECK_EQ(drawer.ops()[5].x0, 45.0f);
    CHECK_EQ(drawer.ops()[5].y0, 88.0f);
    CHECK_EQ(drawer.ops()[6].x0, 55.0f);

    // The same picture wherever the page is put: with the origin 0.3, 0.5
    // and 0.9 of a pixel along, the edge goes to floor(0.3 + 0.5) = 0,
    // floor(0.5 + 0.5) = 1 and floor(0.9 + 0.5) = 1, and everything with it.
    const std::vector<DrawOp> at_zero = drawer.ops();
    for (const float along : {0.3f, 0.5f, 0.9f}) {
        CHECK_CONTEXT("origin %g", static_cast<double>(along));
        args.origin = ImVec2(along, 0.0f);
        drawer.place(&run, 1, args, binder);
        REQUIRE_EQ(drawer.ops().size(), at_zero.size());
        const float moved = along < 0.5f ? 0.0f : 1.0f;
        for (size_t i = 0; i < 6; ++i) {
            CHECK_EQ(drawer.ops()[i].x0, at_zero[i].x0 + moved);
            CHECK_EQ(drawer.ops()[i].y0, at_zero[i].y0);
        }
        CHECK_EQ(drawer.ops()[4].x1, at_zero[4].x1 + moved);
    }
    args.origin = ImVec2(0.0f, 0.0f);

    // A rule that is no formula's is snapped alone under both sets of rules,
    // and ends the span: one before any glyph of its line, and one after a
    // glyph of the text. A formula's rule shipped before any of its glyphs
    // (the bar of an overline) has its ends from the formula's edge and its
    // top and bottom by its own edges, there being no baseline yet. And one
    // thinner than a pixel keeps one pixel of ink.
    std::vector<DrawCmd> alone;
    alone.push_back(line_cmd(0, sp(100.3), sp(200.0), sp(20.0), sp(5.0)));
    alone.push_back(DrawCmd::rule_rect(sp(41.6), sp(85.6), sp(20.0), sp(0.9)));
    alone.push_back(DrawCmd::glyph(key, 't', sp(1.4), sp(100.3), 0));
    alone.push_back(DrawCmd::rule_rect(sp(41.6), sp(85.6), sp(20.0), sp(0.9)));
    alone.push_back(DrawCmd::atom(sp(3.2), sp(100.3), sp(9.0), sp(9.0), sp(15.0), sp(3.0), 1));
    alone.push_back(DrawCmd::rule_rect(sp(3.2), sp(85.6), sp(7.4), sp(0.9)));
    alone.push_back(DrawCmd::glyph(key, 'a', sp(3.2), sp(100.3), 1));
    alone.push_back(DrawCmd::rule_rect(sp(10.2), sp(90.1), sp(0.2), sp(0.2)));
    run.cmds = alone.data();
    run.count = alone.size();
    drawer.place(&run, 1, args, binder);
    REQUIRE_EQ(drawer.ops().size(), size_t(6));
    CHECK_EQ(drawer.ops()[0].x0, 42.0f);
    CHECK_EQ(drawer.ops()[0].y0, 86.0f);
    CHECK_EQ(drawer.ops()[1].x0, 1.0f);
    CHECK_EQ(drawer.ops()[2].x0, 42.0f);
    CHECK_EQ(drawer.ops()[2].y0, 86.0f);
    // The formula's edge at floor(3.2 + 0.5) = 3. The overline from 3 to
    // 3 + floor(7.4 + 0.5) = 10, and floor(85.6 + 0.5) = 86 to
    // floor(86.5 + 0.5) = 87.
    CHECK_EQ(drawer.ops()[3].x0, 3.0f);
    CHECK_EQ(drawer.ops()[3].x1, 10.0f);
    CHECK_EQ(drawer.ops()[3].y0, 86.0f);
    CHECK_EQ(drawer.ops()[3].y1, 87.0f);
    CHECK_EQ(drawer.ops()[4].x0, 3.0f);
    // 3 + floor(7.0 + 0.5) = 10 to 3 + floor(7.2 + 0.5) = 10, widened to 11;
    // floor(100 - 10.2 + 0.5) = 90 to floor(100 - 10.0 + 0.5) = 90, to 91.
    CHECK_EQ(drawer.ops()[5].x0, 10.0f);
    CHECK_EQ(drawer.ops()[5].x1, 11.0f);
    CHECK_EQ(drawer.ops()[5].y0, 90.0f);
    CHECK_EQ(drawer.ops()[5].y1, 91.0f);

    // A fraction whose box starts at 48.0: five glyphs of numerator 10.4
    // apart from 50.0, the bar from 48.0 to 104.0 under them, one glyph of
    // denominator at 70.0.
    std::vector<DrawCmd> fraction;
    fraction.push_back(line_cmd(0, sp(100.3), sp(200.0), sp(20.0), sp(20.0)));
    fraction.push_back(
        DrawCmd::atom(sp(48.0), sp(100.3), sp(56.0), sp(56.0), sp(15.0), sp(15.0), 2));
    for (uint32_t i = 0; i < 5; ++i)
        fraction.push_back(DrawCmd::glyph(key, 'a' + i, sp(50.0 + 10.4 * i), sp(100.3), 2));
    fraction.push_back(DrawCmd::rule_rect(sp(48.0), sp(95.0), sp(56.0), sp(1.0)));
    fraction.push_back(DrawCmd::glyph(key, 'z', sp(70.0), sp(100.3), 2));
    run.cmds = fraction.data();
    run.count = fraction.size();
    // The product, from the edge at 48:
    //   the numerator: 48 + floor(2.0 + 10.4 i + 0.5) = 50, 60, 71, 81, 92
    //   the bar: 48 to 48 + floor(56.0 + 0.5) = 104
    //   y: floor(100 + (95.0 - 100.3) + 0.5) = 95 to floor(100 + (96.0 - 100.3) + 0.5) = 96
    //   the denominator: 48 + floor(22.0 + 0.5) = 70
    drawer.place(&run, 1, args, binder);
    REQUIRE_EQ(drawer.ops().size(), size_t(7));
    CHECK_EQ(drawer.ops()[0].x0, 50.0f);
    CHECK_EQ(drawer.ops()[1].x0, 60.0f);
    CHECK_EQ(drawer.ops()[2].x0, 71.0f);
    CHECK_EQ(drawer.ops()[3].x0, 81.0f);
    CHECK_EQ(drawer.ops()[4].x0, 92.0f);
    CHECK_EQ(drawer.ops()[5].x0, 48.0f);
    CHECK_EQ(drawer.ops()[5].x1, 104.0f);
    CHECK_EQ(drawer.ops()[5].y0, 95.0f);
    CHECK_EQ(drawer.ops()[5].y1, 96.0f);
    CHECK_EQ(drawer.ops()[6].x0, 70.0f);
    // The reference: the numerator at 50, 60, 70, 80, 90, its last glyph 1.6
    // short of its place; the bar by its own corners, 48 to 104; the
    // denominator the anchor of a new span, at 70.
    args.rules = DrawRules::reference();
    drawer.place(&run, 1, args, binder);
    REQUIRE_EQ(drawer.ops().size(), size_t(7));
    CHECK_EQ(drawer.ops()[4].x0, 90.0f);
    CHECK_EQ(drawer.ops()[5].x0, 48.0f);
    CHECK_EQ(drawer.ops()[5].x1, 104.0f);
    CHECK_EQ(drawer.ops()[6].x0, 70.0f);
}

TEST(d7_a_rule_under_a_wider_row_is_placed_with_its_own_box_and_not_the_row_above)
{
    // Rows of a formula overlap along the line, and are shipped one after
    // the other. One TeX point to the pixel.
    const auto sp = [](double pixels) { return static_cast<Scaled>(pixels * 65536.0); };
    const FontKey key{1};
    DrawArgs args;
    args.px_per_inch = PT_PER_INCH;
    args.rules = DrawRules::product();
    FixedBinder binder;
    Drawer drawer;
    DrawRun run;

    // A radical under a wider numerator, the box from 50.0: twenty-one
    // glyphs 10.4 apart from 50.0, the fraction's bar under them, then a
    // sign 10 wide at 150.2, its bar from 160.2 to 168.2 and an x at 160.2.
    //   the numerator: 50 + floor(10.4 i + 0.5), the last at 50 + 208 = 258
    //   the fraction's bar: 50 to 50 + floor(218.4 + 0.5) = 268
    //   the sign: 50 + floor(100.2 + 0.5) = 150, so its right edge at 160
    //   its bar: 50 + floor(110.2 + 0.5) = 160 to 50 + floor(118.2 + 0.5) = 168
    //   the x: 160
    std::vector<DrawCmd> under;
    under.push_back(line_cmd(0, sp(100.3), sp(400.0), sp(20.0), sp(20.0)));
    under.push_back(
        DrawCmd::atom(sp(50.0), sp(100.3), sp(218.4), sp(218.4), sp(15.0), sp(15.0), 2));
    for (uint32_t i = 0; i < 21; ++i)
        under.push_back(DrawCmd::glyph(key, 'a' + i, sp(50.0 + 10.4 * i), sp(90.3), 2));
    under.push_back(DrawCmd::rule_rect(sp(50.0), sp(95.0), sp(218.4), sp(1.0)));
    under.push_back(DrawCmd::glyph(key, 'S', sp(150.2), sp(110.3), 2));
    under.push_back(DrawCmd::rule_rect(sp(160.2), sp(98.0), sp(8.0), sp(1.0)));
    under.push_back(DrawCmd::glyph(key, 'x', sp(160.2), sp(110.3), 2));
    run.cmds = under.data();
    run.count = under.size();
    drawer.place(&run, 1, args, binder);
    REQUIRE_EQ(drawer.ops().size(), size_t(25));
    CHECK_EQ(drawer.ops()[0].x0, 50.0f);
    CHECK_EQ(drawer.ops()[20].x0, 258.0f);
    CHECK_EQ(drawer.ops()[21].x0, 50.0f);
    CHECK_EQ(drawer.ops()[21].x1, 268.0f);
    CHECK_EQ(drawer.ops()[22].x0, 150.0f);
    CHECK_EQ(drawer.ops()[23].x0, 160.0f);
    CHECK_EQ(drawer.ops()[23].x1, 168.0f);
    CHECK_EQ(drawer.ops()[24].x0, 160.0f);

    // A fraction under the same numerator: an x at 150.2, its bar from 150.2
    // to 158.2, a y at 150.2.
    //   the x and the y: 50 + floor(100.2 + 0.5) = 150
    //   the bar: 150 to 50 + floor(108.2 + 0.5) = 158
    std::vector<DrawCmd> nested(under.begin(), under.begin() + 24);
    nested.push_back(DrawCmd::glyph(key, 'x', sp(150.2), sp(105.3), 2));
    nested.push_back(DrawCmd::rule_rect(sp(150.2), sp(108.0), sp(8.0), sp(1.0)));
    nested.push_back(DrawCmd::glyph(key, 'y', sp(150.2), sp(118.3), 2));
    run.cmds = nested.data();
    run.count = nested.size();
    drawer.place(&run, 1, args, binder);
    REQUIRE_EQ(drawer.ops().size(), size_t(25));
    CHECK_EQ(drawer.ops()[21].x1, 268.0f);
    CHECK_EQ(drawer.ops()[22].x0, 150.0f);
    CHECK_EQ(drawer.ops()[23].x0, 150.0f);
    CHECK_EQ(drawer.ops()[23].x1, 158.0f);
    CHECK_EQ(drawer.ops()[24].x0, 150.0f);

    // Two hundred glyphs of numerator 10.4 apart from 50.0 over one glyph:
    // however many came before, the bar starts at the box's edge, 50, and
    // ends at 50 + floor(2080.0 + 0.5) = 2130, where the last glyph, at
    // 50 + floor(2069.6 + 0.5) = 2120, has the 10.4 it is wide.
    std::vector<DrawCmd> wide;
    wide.push_back(line_cmd(0, sp(100.3), sp(4000.0), sp(20.0), sp(20.0)));
    wide.push_back(
        DrawCmd::atom(sp(50.0), sp(100.3), sp(2080.0), sp(2080.0), sp(15.0), sp(15.0), 2));
    for (uint32_t i = 0; i < 200; ++i)
        wide.push_back(DrawCmd::glyph(key, 'a' + i % 26, sp(50.0 + 10.4 * i), sp(90.3), 2));
    wide.push_back(DrawCmd::rule_rect(sp(50.0), sp(95.0), sp(2080.0), sp(1.0)));
    wide.push_back(DrawCmd::glyph(key, '2', sp(1085.0), sp(110.3), 2));
    run.cmds = wide.data();
    run.count = wide.size();
    drawer.place(&run, 1, args, binder);
    REQUIRE_EQ(drawer.ops().size(), size_t(202));
    CHECK_EQ(drawer.ops()[0].x0, 50.0f);
    CHECK_EQ(drawer.ops()[199].x0, 2120.0f);
    CHECK_EQ(drawer.ops()[200].x0, 50.0f);
    CHECK_EQ(drawer.ops()[200].x1, 2130.0f);
    CHECK_EQ(drawer.ops()[201].x0, 1085.0f);
}

TEST(d7_pictures_of_the_joint_for_whoever_sets_IMTYPESET_HARDEN_SHOTS)
{
    // Not a check: pixels to look at. With IMTYPESET_HARDEN_SHOTS=<directory>
    // it writes one picture for each of a few texts, sizes and lengths: the
    // text under the reference's rules above the same under the product's,
    // from a little left of its first rule or of its formula, four times
    // enlarged.
    const char* const directory = std::getenv("IMTYPESET_HARDEN_SHOTS");
    if (directory == nullptr || directory[0] == '\0') {
        std::printf("    SKIPPED: IMTYPESET_HARDEN_SHOTS is not set\n");
        return;
    }
    const int page_width = 2400, page_height = 96;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(page_width), static_cast<float>(page_height));
    io.DeltaTime = 1.0f / 60.0f;
    soft_raster::Renderer renderer;
    renderer.init();
    REQUIRE(ImTypeset::CreateContext() != nullptr);
    for (const auto& entry : DEFAULT_FONTS) {
        const std::string path = golden::data_path(std::string("fonts/") + entry.file);
        REQUIRE(ImTypeset::AddFontFromFileTTF(entry.face, path.c_str()) != nullptr);
    }
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    const struct {
        const char* name;
        float em;
        int terms;
        // What follows the `terms` of "a+b+...": the end of one formula, or,
        // with no terms, a text with formulas in it.
        const char* rest;
    } shots[] = {
        {"root", 11.0f, 13, "\\sqrt{x}$"},
        {"root", 11.0f, 26, "\\sqrt{x}$"},
        {"root", 13.0f, 0, "\\sqrt{x}$"},
        {"root", 13.0f, 13, "\\sqrt{x}$"},
        {"root", 13.0f, 26, "\\sqrt{x}$"},
        {"root", 13.0f, 60, "\\sqrt{x}$"},
        {"root", 24.0f, 0, "\\sqrt{x}$"},
        {"root", 24.0f, 13, "\\sqrt{x}$"},
        {"quadratic", 17.0f, 0, "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$"},
        {"quadratic", 17.0f, 13, "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$"},
        {"quadratic", 17.0f, 40, "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$"},
        {"quadratic", 19.0f, 0, "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$"},
        {"wide-denominator", 17.0f, 40, "\\frac{1}{abcdefghijkl}$"},
        {"root-under-numerator", 16.0f, 0, "\\frac{a+b+c+d+e+f+g+h+i+j}{\\sqrt{x}}$"},
        {"root-under-numerator", 19.0f, 13, "\\frac{a+b+c+d+e+f+g+h+i+j}{\\sqrt{x}}$"},
        {"root-over-denominator", 19.0f, 13, "\\frac{\\sqrt{x}}{abcdefghijkl}$"},
        {"fraction-under-numerator", 19.0f, 13, "\\frac{abcdefghijkl}{\\frac{x}{y}}$"},
        {"one-over-ones", 16.0f, 0, "\\frac{1}{11111111111}$"},
        {"one-over-ones", 19.0f, 13, "\\frac{1}{11111111111}$"},
        {"underline-overline", 19.0f, 13, "\\underline{abcdefgh}+\\overline{abcdefgh}$"},
        {"scripts-on-root", 19.0f, 13, "\\sqrt{a^2+b_i^2}^3+\\sqrt{x}^2$"},
        {"letters", 13.0f, 0, "abcdefghijklmnopqrstuvwxyz+\\sin(2\\pi x)+1234567890$"},
        {"letters", 16.0f, 0, "abcdefghijklmnopqrstuvwxyz+\\sin(2\\pi x)+1234567890$"},
        {"letters", 19.0f, 0, "abcdefghijklmnopqrstuvwxyz+\\sin(2\\pi x)+1234567890$"},
        {"in-text-root-of-fraction", 19.0f, 0,
         "the root $\\sqrt{\\frac{a}{b}}$ of it and $\\sqrt{x}$ alone"},
        {"in-text-fraction", 19.0f, 0,
         "Formulas are part of the text, $\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$ for one, and "
         "$x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$ for another."},
        {"in-text-fraction", 16.0f, 0,
         "Formulas are part of the text, $\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}$ for one, and "
         "$x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$ for another."},
        {"in-text-integral", 19.0f, 0,
         "that $\\int f(x) dx$ and $\\int_0^1 x^2 dx = \\frac{1}{3}$ are"},
    };
    const int zoom = 4;
    for (const auto& shot : shots) {
        // One `$` in it closes a formula that is opened here.
        const std::string rest = shot.rest;
        std::string text = std::count(rest.begin(), rest.end(), '$') == 1 ? "$" : "";
        for (int i = 0; i < shot.terms; ++i) {
            text += static_cast<char>('a' + i % 26);
            text += '+';
        }
        text += shot.rest;
        ImTypesetStyle style = ImTypeset::GetStyle();
        style.EmSize = shot.em;
        style.Color = IM_COL32(0, 0, 0, 255);
        ImTypeset::SetParagraphText(p, text.c_str(), nullptr, nullptr, 0, ImTypesetTextFlags_Math);
        ImTypeset::LayoutParagraph(p, 100000.0f, &style);
        soft_raster::Image pages[2];
        int from = 0, to = page_width;
        for (const bool product : {false, true}) {
            ImTypeset::GetCurrentContext()->draw_rules =
                product ? DrawRules::product() : DrawRules::reference();
            ImGui::NewFrame();
            ImDrawList* list = ImGui::GetBackgroundDrawList();
            ImTypeset::DrawParagraph(p, list, ImVec2(3.0f, 30.0f), nullptr);
            // From a little left of the leftmost rule when glyphs were put
            // before the formula's end to move it, and of everything
            // otherwise, to a little right of everything.
            float left = 1.0e9f, right = 0.0f, bar = 1.0e9f;
            for (const Quad& quad : quads_of(*list)) {
                left = std::min(left, quad.x0);
                right = std::max(right, quad.x1);
                if (quad.white())
                    bar = std::min(bar, quad.x0);
            }
            if (!product) {
                from = static_cast<int>(shot.terms > 0 && bar < 1.0e8f ? bar - 40.0f : left - 4.0f);
                to = std::min(static_cast<int>(right) + 6, from + 560);
            }
            ImGui::Render();
            pages[product ? 1 : 0] =
                soft_raster::Image(page_width, page_height, IM_COL32(255, 255, 255, 255));
            renderer.render(ImGui::GetDrawData(), pages[product ? 1 : 0]);
        }
        const int rows = 72; // of each page, from its top
        soft_raster::Image sheet((to - from) * zoom, 2 * rows * zoom, IM_COL32(255, 255, 255, 255));
        for (int half = 0; half < 2; ++half) {
            for (int y = 0; y < rows * zoom; ++y) {
                for (int x = 0; x < sheet.width; ++x) {
                    const int sx = from + x / zoom;
                    if (pages[half].contains(sx, y / zoom))
                        std::memcpy(sheet.pixel(x, half * rows * zoom + y),
                                    pages[half].pixel(sx, y / zoom), 4);
                }
            }
        }
        const std::string out = check::format("%s/d7-%s-em%.0f-terms%d.png", directory, shot.name,
                                              static_cast<double>(shot.em), shot.terms);
        CHECK(soft_raster::write_png(out.c_str(), sheet));
        std::printf("    wrote %s (the reference's rules above, the product's below)\n",
                    out.c_str());
    }
    ImTypeset::DestroyParagraph(p);
    ImTypeset::DestroyContext();
    renderer.shutdown();
    ImGui::DestroyContext();
}

// --- D7: every rule of a formula against the ink of its own box -----------------------

namespace
{

// One glyph or one rule of a formula as a draw put it: where the commands have
// it, in scaled points, and the pixels it went to.
struct InkGlyph {
    Scaled x = 0;
    Scaled y = 0;
    double pen = 0.0;
    double pen_y = 0.0;
    uint32_t codepoint = 0;
};
struct InkRule {
    Scaled x = 0, y = 0, width = 0, height = 0;
    double x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0;
    size_t glyphs_before = 0;
};
struct Ink {
    std::vector<InkGlyph> glyphs;
    std::vector<InkRule> rules;
};

// The ops of the draw the context last made, against the commands they came
// of: a TEXT op per glyph, a RECT_FILLED per rule, in the commands' order.
bool ink_of(const std::vector<DrawCmd>& cmds, const std::vector<DrawOp>& ops, Ink& out)
{
    out.glyphs.clear();
    out.rules.clear();
    size_t at = 0;
    for (const DrawCmd& cmd : cmds) {
        if (cmd.is(CmdType::GLYPH)) {
            if (at >= ops.size() || ops[at].type != OpType::TEXT)
                return false;
            out.glyphs.push_back(InkGlyph{cmd.x, cmd.y, static_cast<double>(ops[at].x0),
                                          static_cast<double>(ops[at].y0), cmd.codepoint});
            ++at;
        } else if (cmd.is(CmdType::RULE_RECT)) {
            if (at >= ops.size() || ops[at].type != OpType::RECT_FILLED)
                return false;
            const DrawOp& op = ops[at];
            out.rules.push_back(InkRule{cmd.x, cmd.y, cmd.width, cmd.height,
                                        static_cast<double>(op.x0), static_cast<double>(op.x1),
                                        static_cast<double>(op.y0), static_cast<double>(op.y1),
                                        out.glyphs.size()});
            ++at;
        }
    }
    return at == ops.size();
}

// What one rule of a formula is held against, as glyph numbers in the order
// the formula's glyphs are shipped, counted from the formula's first:
//   left, right   the glyphs of its own box that its two ends belong with: a
//                 radical's sign and the last glyph under the bar; the first
//                 and the last glyph of a fraction's wider part.
//   part_first,   the first and last glyph of what is centred against it: a
//   part_last     fraction's narrower part. -1 for none.
struct OwnBox {
    int left, right, part_first, part_last;
};

struct RuledFormula {
    const char* name;
    std::string source;
    std::vector<OwnBox> rules; // in the order the rules are shipped
};

std::string letters(size_t count)
{
    std::string out;
    for (size_t i = 0; i < count; ++i)
        out += static_cast<char>('a' + i % 26);
    return out;
}

std::vector<RuledFormula> ruled_formulas()
{
    std::vector<RuledFormula> all;
    // Numerator a+...+j is glyphs 0..18; then the sign (19) and x (20).
    all.push_back({"radical under a wider numerator",
                   "\\frac{a+b+c+d+e+f+g+h+i+j}{\\sqrt{x}}",
                   {{0, 18, 19, 20}, {19, 20, -1, -1}}});
    // The sign (0), x (1), then twelve letters.
    all.push_back({"radical over a wider denominator",
                   "\\frac{\\sqrt{x}}{abcdefghijkl}",
                   {{0, 1, -1, -1}, {2, 13, 0, 1}}});
    all.push_back({"fraction under a wider numerator",
                   "\\frac{abcdefghijkl}{\\frac{x}{y}}",
                   {{0, 11, 12, 13}, {12, 12, 13, 13}}});
    all.push_back({"one over eleven ones", "\\frac{1}{11111111111}", {{1, 11, 0, 0}}});
    all.push_back({"seventy letters over 2", "\\frac{" + letters(70) + "}{2}", {{0, 69, 70, 70}}});
    all.push_back({"2 over seventy letters", "\\frac{2}{" + letters(70) + "}", {{1, 70, 0, 0}}});
    all.push_back({"underline", "\\underline{abcdefgh}", {{0, 7, -1, -1}}});
    all.push_back({"overline", "\\overline{abcdefgh}", {{0, 7, -1, -1}}});
    // The sign (0); a 2 + b (1..4), b's 2 and its i (5, 6); then the 3. The 2
    // is the glyph furthest along under the bar.
    all.push_back({"scripts in and on a radical", "\\sqrt{a^2+b_i^2}^3", {{0, 5, -1, -1}}});
    // x = (0, 1); - b ± (2..4); the sign (5); b 2 - 4 a c (6..11); 2 a (12, 13).
    all.push_back({"the quadratic formula",
                   "x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}",
                   {{5, 11, -1, -1}, {2, 11, 12, 13}}});
    all.push_back(
        {"fraction under a radical", "\\sqrt{\\frac{a}{b}}", {{0, 1, -1, -1}, {1, 1, 2, 2}}});
    all.push_back({"twelve letters over 2", "\\frac{abcdefghijkl}{2}", {{0, 11, 12, 12}}});
    all.push_back({"1 over twelve letters", "\\frac{1}{abcdefghijkl}", {{1, 12, 0, 0}}});
    // a + b (0..2) over c + d (3..5): the denominator is the wider by a hair.
    all.push_back({"a+b over c+d", "\\frac{a+b}{c+d}", {{3, 5, 0, 2}}});
    return all;
}

// How far each of a rule's ends is, in pixels, from where the commands put it
// against a glyph of its own box as that glyph was drawn, and how far off the
// middle of the rule the middle of the part centred on it is.
struct RuleErrors {
    double left = 0.0, right = 0.0, centre = 0.0;
};

RuleErrors rule_errors(const Ink& ink, const InkRule& rule, const OwnBox& own, size_t lead,
                       double density)
{
    const auto px = [density](Scaled length) { return px_from_sp(length, density); };
    const auto glyph = [&](int number) -> const InkGlyph& {
        return ink.glyphs[lead + static_cast<size_t>(number)];
    };
    const Scaled end = rule.x + rule.width;
    RuleErrors errors;
    errors.left = (rule.x0 - glyph(own.left).pen) - px(rule.x - glyph(own.left).x);
    errors.right = (rule.x1 - glyph(own.right).pen) - px(end - glyph(own.right).x);
    if (own.part_first >= 0) {
        const double from_left =
            (glyph(own.part_first).pen - rule.x0) - px(glyph(own.part_first).x - rule.x);
        const double from_right =
            (rule.x1 - glyph(own.part_last).pen) - px(end - glyph(own.part_last).x);
        errors.centre = (from_left - from_right) / 2.0;
    }
    return errors;
}

} // namespace

TEST(d7_every_rule_of_a_formula_stands_where_it_should_against_the_ink_of_its_own_box)
{
    // What is held, under the rules the public API draws with, for every rule
    // of every formula here, at seven sizes, after 0, 13 and 40 terms and at
    // eight sub-pixel origins, with the bundled math face: each end of the
    // rule within a pixel of where the commands put it against the glyph of
    // its own box that belongs at that end, as that glyph was drawn, and the
    // part centred on the rule within a pixel of its middle. And what makes
    // it so: every glyph of the formula and every end of a rule within half
    // a pixel of its place from the pixel the formula's left edge was put
    // on, and the formula the same picture at every origin. Under the
    // reference's rules the same distances are measured and printed; they
    // grow with what came before.
    Stage stage;
    REQUIRE(stage.ctx != nullptr);
    ImTypesetParagraph* p = ImTypeset::CreateParagraph();
    REQUIRE(p != nullptr);
    const std::vector<RuledFormula> formulas = ruled_formulas();
    for (const bool product : {false, true}) {
        stage.ctx->draw_rules = product ? DrawRules::product() : DrawRules::reference();
        RuleErrors worst_of_all;
        // Pairs of glyphs shipped one after the other on one baseline with
        // no rule between them, and how many of them stand more than half a
        // pixel from the distance apart the commands give them.
        long pairs = 0, uneven = 0;
        double worst_pair = 0.0;
        for (const RuledFormula& f : formulas) {
            std::vector<RuleErrors> worst(f.rules.size());
            long measured = 0;
            for (const float em : {11.0f, 13.0f, 16.0f, 17.0f, 19.0f, 24.0f, 32.0f}) {
                for (const size_t terms : {size_t(0), size_t(13), size_t(40)}) {
                    CHECK_CONTEXT("%s rules, %s, after %zu terms at %g px",
                                  product ? "product" : "reference", f.name, terms,
                                  static_cast<double>(em));
                    std::string text = "$";
                    for (size_t i = 0; i < terms; ++i) {
                        text += static_cast<char>('a' + i % 26);
                        text += '+';
                    }
                    text += f.source;
                    text += '$';
                    ImTypesetStyle style = ImTypeset::GetStyle();
                    style.EmSize = em;
                    REQUIRE(ImTypeset::SetParagraphText(p, text.c_str(), nullptr, nullptr, 0,
                                                        ImTypesetTextFlags_Math));
                    REQUIRE(ImTypeset::LayoutParagraph(p, 100000.0f, &style));
                    REQUIRE(!ImTypeset::GetParagraphFormula(p, 0).Refused);
                    const std::vector<DrawCmd>& cmds = shipped_commands(*p);
                    const double density = p->layout_style.px_per_inch;
                    const DrawCmd* atom = nullptr;
                    for (const DrawCmd& cmd : cmds)
                        atom = atom == nullptr && cmd.is(CmdType::ATOM) ? &cmd : atom;
                    REQUIRE(atom != nullptr);
                    std::vector<double> at_first_origin;
                    for (int step = 0; step < 8; ++step) {
                        Frame frame;
                        ImDrawList* list = ImGui::GetBackgroundDrawList();
                        list->PushClipRect(ImVec2(-1.0e6f, -1.0e6f), ImVec2(1.0e6f, 1.0e6f));
                        const ImVec2 origin(3.0f + 0.125f * static_cast<float>(step), 40.0f);
                        REQUIRE(ImTypeset::DrawParagraph(p, list, origin, nullptr));
                        list->PopClipRect();
                        Ink ink;
                        REQUIRE(ink_of(cmds, stage.ctx->drawer.ops(), ink));
                        REQUIRE_EQ(ink.rules.size(), f.rules.size());
                        const size_t lead = 2 * terms;
                        for (size_t r = 0; r < f.rules.size(); ++r) {
                            const RuleErrors e =
                                rule_errors(ink, ink.rules[r], f.rules[r], lead, density);
                            worst[r].left = std::max(worst[r].left, std::fabs(e.left));
                            worst[r].right = std::max(worst[r].right, std::fabs(e.right));
                            worst[r].centre = std::max(worst[r].centre, std::fabs(e.centre));
                            if (product) {
                                CHECK(std::fabs(e.left) <= 1.0);
                                CHECK(std::fabs(e.right) <= 1.0);
                                CHECK(std::fabs(e.centre) <= 1.0);
                            }
                        }
                        std::vector<bool> after_a_rule(ink.glyphs.size() + 1, false);
                        for (const InkRule& rule : ink.rules)
                            after_a_rule[rule.glyphs_before] = true;
                        for (size_t g = 1; g < ink.glyphs.size(); ++g) {
                            const InkGlyph& now = ink.glyphs[g];
                            const InkGlyph& before = ink.glyphs[g - 1];
                            if (now.y != before.y || now.x <= before.x || after_a_rule[g])
                                continue;
                            const double off = std::fabs((now.pen - before.pen) -
                                                         px_from_sp(now.x - before.x, density));
                            ++pairs;
                            uneven += off > 0.5 ? 1 : 0;
                            worst_pair = std::max(worst_pair, off);
                        }
                        if (product) {
                            // From the pixel the formula's edge went to.
                            const double edge =
                                snap_px(device_coordinate(origin.x, atom->x, density));
                            std::vector<double> offsets;
                            for (const InkGlyph& glyph : ink.glyphs) {
                                offsets.push_back(glyph.pen - edge);
                                CHECK(std::fabs(offsets.back() -
                                                px_from_sp(glyph.x - atom->x, density)) <= 0.5);
                            }
                            for (const InkRule& rule : ink.rules) {
                                offsets.push_back(rule.x0 - edge);
                                offsets.push_back(rule.x1 - edge);
                                CHECK(std::fabs(offsets[offsets.size() - 2] -
                                                px_from_sp(rule.x - atom->x, density)) <= 0.5);
                                CHECK(std::fabs(offsets.back() -
                                                px_from_sp(rule.x + rule.width - atom->x,
                                                           density)) <= 0.5);
                            }
                            if (step == 0)
                                at_first_origin = offsets;
                            CHECK(offsets == at_first_origin);
                        }
                        ++measured;
                    }
                }
            }
            for (size_t r = 0; r < worst.size(); ++r) {
                std::printf("    %-9s %-34s rule %zu, %ld draws: left %.2f  right %.2f  centre "
                            "%.2f px\n",
                            product ? "product" : "reference", f.name, r, measured, worst[r].left,
                            worst[r].right, worst[r].centre);
                worst_of_all.left = std::max(worst_of_all.left, worst[r].left);
                worst_of_all.right = std::max(worst_of_all.right, worst[r].right);
                worst_of_all.centre = std::max(worst_of_all.centre, worst[r].centre);
            }
        }
        std::printf("    %-9s in all: left %.2f  right %.2f  centre %.2f px; of %ld pairs of "
                    "glyphs side by side %ld are more than half a pixel from their distance "
                    "apart, the furthest %.2f px\n",
                    product ? "product" : "reference", worst_of_all.left, worst_of_all.right,
                    worst_of_all.centre, pairs, uneven, worst_pair);
        // The reference's are what the rule is for: if they were small there
        // would be nothing here to hold. And what the rule costs is the
        // evenness the reference has: no pair of its glyphs is off.
        if (!product) {
            CHECK(worst_of_all.left > 1.5);
            CHECK(worst_of_all.right > 1.5);
            CHECK(worst_of_all.centre > 1.5);
            CHECK_EQ(uneven, 0L);
        } else {
            CHECK(worst_pair < 1.0);
        }
    }
    ImTypeset::DestroyParagraph(p);
}
