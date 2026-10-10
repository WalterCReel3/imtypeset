// src/mathtext: the `$...$` subset parser.
//
// Replays tests/golden/math/mathtext.golden (tools/oracle/FORMAT-math.md
// section 5): 3,776 sources, each with the math list the reference built for
// it, compared item for item, or the refusal it answered, compared kind,
// position (in codepoints and in bytes) and reason, character for character.
// Then formulas.golden end to end: source to atoms to boxes to commands, and
// what `typeset` says when the engine will not set a formula that parsed.
//
// Then what is this port's own: the tables against the reference's lists, the
// sources of the reference's hostile-size tests at their real sizes, what a
// source that is not UTF-8 becomes, and the reasons `typeset` gives for a
// formula whose words cannot be set, which no golden holds and which were
// asked of the reference directly.

#include "math_golden.h"

#include "mathtext.h"
#include "pack.h"
#include "shipout.h"

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

RefusalKind kind_of(std::string_view value)
{
    for (int i = 0; i < REFUSAL_KIND_COUNT; ++i) {
        if (value == refusal_kind_value(static_cast<RefusalKind>(i)))
            return static_cast<RefusalKind>(i);
    }
    FAIL("not a RefusalKind value: %.*s", static_cast<int>(value.size()), value.data());
    return RefusalKind::EMPTY;
}

std::string repeated(std::string_view piece, size_t count)
{
    std::string out;
    out.reserve(piece.size() * count);
    for (size_t i = 0; i < count; ++i)
        out += piece;
    return out;
}

struct Parsed {
    Formula formula;
    Refusal refusal;
    bool ok = false;

    explicit Parsed(std::string_view source, bool display = false)
    {
        ok = parse(source, display, formula, refusal);
    }
};

// How deep a list nests: 1 for a list of plain atoms, and one more for each
// field on the way down that is a list. Every field of every item is followed.
int list_depth(const MathList& list, MathView items)
{
    int deepest = 0;
    for (size_t i = 0; i < items.size; ++i) {
        for (const Field* field : {&items[i].nucleus, &items[i].sup, &items[i].sub}) {
            MathView below;
            if (field->type == FieldType::SUB_LIST && list.view(field->span, &below)) {
                const int depth = list_depth(list, below);
                deepest = depth > deepest ? depth : deepest;
            }
        }
    }
    return 1 + deepest;
}

// A refusal as one line, for comparing whole.
std::string line(const Refusal& refusal)
{
    return check::format("%s %zu %zu %s", refusal_kind_value(refusal.kind), refusal.position,
                         refusal.byte_position, refusal.reason.c_str());
}

std::string refused(std::string_view source)
{
    Parsed parsed(source);
    if (parsed.ok)
        return "<formula>";
    return line(parsed.refusal);
}

} // namespace

// --- mathtext.golden -----------------------------------------------------------------

TEST(the_tables_are_the_references)
{
    golden::File file;
    REQUIRE(file.load(golden::path("math/mathtext.golden")));
    const std::vector<core_golden::Case> cases = core_golden::cases(file);
    REQUIRE(!cases.empty());
    REQUIRE_EQ(cases[0].name, std::string("tables"));
    core_golden::Reader reader(file, cases[0]);

    std::vector<std::string> supported;
    std::vector<std::string> refused_commands;
    std::set<std::string> sized;
    std::vector<std::string> kinds;
    std::set<std::string> prose;
    while (!reader.done()) {
        const golden::Record& r = reader.take();
        CHECK_CONTEXT("mathtext.golden line %d", r.line);
        if (r.is("supported")) {
            supported.push_back(r.str(0));
        } else if (r.is("refused_command")) {
            refused_commands.push_back(r.str(0) + " " + std::string(r.raw(1)) + " " + r.str(2));
        } else if (r.is("sized_delimiter")) {
            sized.insert(r.str(0) + " " + check::format("%lld", static_cast<long long>(r.i64(1))));
        } else if (r.is("refusal_kind")) {
            kinds.push_back(std::string(r.raw(0)));
        } else if (r.is("prose_command")) {
            prose.insert(r.str(0) + " " + r.str(1));
        } else if (r.is("const")) {
            REQUIRE_EQ(std::string(r.raw(0)), std::string("max_depth"));
            CHECK_EQ(r.i64(2), MAX_DEPTH);
        } else {
            FAIL("a record this test does not know: %.*s", static_cast<int>(r.tag.size()),
                 r.tag.data());
        }
    }

    std::vector<std::string> ours;
    for (size_t i = 0; i < supported_command_count(); ++i)
        ours.push_back(supported_command(i));
    CHECK_LINES_EQ(supported, ours, "the supported commands");
    CHECK(supported.size() > 200);
    CHECK_EQ(std::string(supported_command(supported_command_count())), std::string());

    ours.clear();
    for (size_t i = 0; i < refused_command_count(); ++i) {
        const RefusedCommand command = refused_command(i);
        ours.push_back(std::string(command.name) + " " + refusal_kind_value(command.kind) + " " +
                       command.reason_tail);
    }
    CHECK_LINES_EQ(refused_commands, ours, "the refused commands");
    CHECK(refused_commands.size() > 90);

    std::set<std::string> ours_set;
    for (size_t i = 0; i < sized_delimiter_count(); ++i) {
        const SizedDelimiter delimiter = sized_delimiter(i);
        ours_set.insert(std::string(delimiter.written) + " " +
                        check::format("%d", delimiter.codepoint));
    }
    CHECK(ours_set == sized);
    CHECK_EQ(sized.size(), size_t(25));

    ours.clear();
    for (int i = 0; i < REFUSAL_KIND_COUNT; ++i)
        ours.push_back(refusal_kind_value(static_cast<RefusalKind>(i)));
    CHECK_LINES_EQ(kinds, ours, "the refusal kinds");
    CHECK_EQ(std::string(refusal_kind_value(static_cast<RefusalKind>(200))), std::string());

    ours_set.clear();
    for (size_t i = 0; i < prose_command_count(); ++i) {
        const ProseCommand command = prose_command(i);
        ours_set.insert(std::string(command.name) + " " + face_value(command.face));
    }
    CHECK(ours_set == prose);
    CHECK_EQ(prose.size(), size_t(5));
}

TEST(every_source_parses_to_the_references_list_or_its_refusal)
{
    golden::File file;
    REQUIRE(file.load(golden::path("math/mathtext.golden")));
    long formulas = 0;
    long refusals = 0;
    Formula formula;
    Refusal refusal;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        if (c.kind != "parse")
            continue;
        CHECK_CONTEXT("mathtext.golden: case %s (line %d)", c.name.c_str(), c.line);
        core_golden::Reader reader(file, c);
        core_golden::Context context;
        const std::string source = reader.take("source").str(0);
        // `display` chooses the style and nothing else: both give the same
        // list or the same refusal.
        for (const bool display : {false, true}) {
            core_golden::Reader expected(file, c);
            expected.take("source");
            const bool ok = parse(source, display, formula, refusal);
            CHECK(formula.display == display);
            CHECK(formula.style() == (display ? Style::DISPLAY : Style::TEXT));
            if (expected.next_is("formula")) {
                expected.take();
                if (!ok) {
                    FAIL("refused what the reference parsed: %s", line(refusal).c_str());
                    continue;
                }
                const size_t start = expected.position();
                CHECK_LINES_EQ(expected.lines(start, expected.end()),
                               math_golden::mlist_lines(context, formula.list, formula.view()),
                               "the math list");
                CHECK(refusal.reason.empty());
                if (!display)
                    ++formulas;
            } else {
                const golden::Record& r = expected.take("refusal");
                if (ok) {
                    FAIL("parsed what the reference refused (%s)", r.str(3).c_str());
                    continue;
                }
                CHECK_EQ(std::string(r.raw(0)), std::string(refusal_kind_value(refusal.kind)));
                CHECK_EQ(r.i64(1), static_cast<int64_t>(refusal.position));
                CHECK_EQ(r.i64(2), static_cast<int64_t>(refusal.byte_position));
                CHECK_EQ(r.str(3), refusal.reason);
                CHECK(kind_of(r.raw(0)) == refusal.kind);
                CHECK(refusal.byte_position <= source.size());
                CHECK_EQ(formula.list.item_count(), size_t(0));
                if (!display)
                    ++refusals;
            }
        }
    }
    std::printf("    mathtext.golden: %ld formulas, %ld refusals\n", formulas, refusals);
    CHECK(formulas >= 1545);
    CHECK(refusals >= 2231);
}

namespace
{

// tex.web section 1186's rule, on which a script after a group depends:
// `{x}_1` hangs its script on the letter, `{x^2}^3` on the group. Each list is
// the reference's, asked of it directly, as the records of a math list joined
// by `|`, or its refusal.
struct Braced {
    const char* source;
    const char* answer;
};

constexpr Braced BRACED[] = {
    {"{x_2}", "mlist 1|atom ord default|sublist 1|atom ord default|mathchar 119909|"
              "empty|sublist 1|atom ord default|mathchar 50|empty|empty|endsublist|"
              "endsublist|empty|empty|endmlist"},
    {"{x^2}", "mlist 1|atom ord default|sublist 1|atom ord default|mathchar 119909|"
              "sublist 1|atom ord default|mathchar 50|empty|empty|endsublist|empty|"
              "endsublist|empty|empty|endmlist"},
    {"{x_2}^3", "mlist 1|atom ord default|sublist 1|atom ord default|mathchar 119909|"
                "empty|sublist 1|atom ord default|mathchar 50|empty|empty|endsublist|"
                "endsublist|sublist 1|atom ord default|mathchar 51|empty|empty|"
                "endsublist|empty|endmlist"},
    {"{x^2}^3", "mlist 1|atom ord default|sublist 1|atom ord default|mathchar 119909|"
                "sublist 1|atom ord default|mathchar 50|empty|empty|endsublist|empty|"
                "endsublist|sublist 1|atom ord default|mathchar 51|empty|empty|"
                "endsublist|empty|endmlist"},
    // Two levels of braces around a letter are still the letter.
    {"{{x}}_1", "mlist 1|atom ord default|mathchar 119909|empty|sublist 1|atom ord "
                "default|mathchar 49|empty|empty|endsublist|endmlist"},
    // An atom of another class keeps its group, and so does an Inner.
    {"{+}", "mlist 1|atom ord default|sublist 1|atom bin default|"
            "mathchar 43|empty|empty|endsublist|empty|empty|endmlist"},
    {"{\\frac{a}{b}}", "mlist 1|atom ord default|sublist 1|atom inner default|sublist 1|"
                       "fraction 0 0|sublist 1|atom ord default|mathchar 119886|empty|empty|"
                       "endsublist|sublist 1|atom ord default|mathchar 119887|empty|empty|"
                       "endsublist|endsublist|empty|empty|endsublist|empty|empty|endmlist"},
    // Words are an ordinary atom, and a prime then a superscript share one.
    {"{\\text{a}}'", "mlist 1|atom ord default|mathtext prose a|sublist 1|atom ord default|"
                     "mathchar 8242|empty|empty|endsublist|empty|endmlist"},
    {"{x}'^2", "mlist 1|atom ord default|mathchar 119909|sublist 2|atom ord default|"
               "mathchar 8242|empty|empty|atom ord default|mathchar 50|empty|empty|"
               "endsublist|empty|endmlist"},
    // The last of \limits and \nolimits wins, and neither follows a group.
    {"\\sum\\nolimits\\limits_a", "mlist 1|atom op limits|mathchar 8721|empty|sublist 1|"
                                  "atom ord default|mathchar 119886|empty|empty|endsublist|"
                                  "endmlist"},
    {"{\\sum}\\limits", "REFUSAL syntax 6 6 \\limits must follow an operator"},
};

std::string joined(const std::vector<std::string>& records)
{
    std::string out;
    for (const std::string& record : records) {
        if (!out.empty())
            out += '|';
        out += record;
    }
    return out;
}

} // namespace

TEST(a_group_holding_one_bare_ordinary_atom_is_that_atom_and_no_other_group_is)
{
    // The `braced` cases of mathtext.golden are these sources. What the golden
    // holds for each is compared with the table, and then the port is.
    golden::File file;
    REQUIRE(file.load(golden::path("math/mathtext.golden")));
    std::map<std::string, std::string> recorded;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        if (c.kind != "parse" || c.name.rfind("braced#", 0) != 0)
            continue;
        core_golden::Reader reader(file, c);
        const std::string source = reader.take("source").str(0);
        std::string answer;
        if (reader.next_is("refusal")) {
            const golden::Record& r = reader.take();
            answer = check::format("REFUSAL %s %lld %lld %s", std::string(r.raw(0)).c_str(),
                                   static_cast<long long>(r.i64(1)),
                                   static_cast<long long>(r.i64(2)), r.str(3).c_str());
        } else {
            reader.take("formula");
            answer = joined(reader.lines(reader.position(), reader.end()));
        }
        CHECK(recorded.emplace(source, answer).second);
    }
    CHECK_EQ(recorded.size(), sizeof(BRACED) / sizeof(BRACED[0]));

    for (const Braced& row : BRACED) {
        CHECK_CONTEXT("source %s", row.source);
        const auto held = recorded.find(row.source);
        if (held == recorded.end())
            FAIL("mathtext.golden has no `braced` case for this source");
        else
            CHECK_EQ(held->second, std::string(row.answer));

        Parsed parsed(row.source);
        std::string ours;
        if (parsed.ok) {
            core_golden::Context context;
            ours = joined(
                math_golden::mlist_lines(context, parsed.formula.list, parsed.formula.view()));
        } else {
            ours = "REFUSAL " + line(parsed.refusal);
        }
        CHECK_EQ(ours, std::string(row.answer));
    }
}

// --- formulas.golden: source to boxes ------------------------------------------------

namespace
{

void replay_formulas(bool recorded_fonts)
{
    golden::File file;
    REQUIRE(file.load(golden::path("math/formulas.golden")));
    FontLibrary library;
    REQUIRE(math_golden::register_reference_fonts(library));
    math_golden::ParamsTable params;
    long set = 0;
    long refusals = 0;
    long by_hand = 0;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        CHECK_CONTEXT("formulas.golden: case %s (line %d), %s fonts", c.name.c_str(), c.line,
                      recorded_fonts ? "recorded" : "library");
        if (c.kind == "table") {
            core_golden::Context context;
            params.read(file, c, context);
            continue;
        }
        core_golden::Reader reader(file, c);
        core_golden::Context context;
        if (!reader.next_is("source")) {
            // A list built by hand, with no source to parse: math-mlist's.
            ++by_hand;
            continue;
        }
        const golden::Record& source_record = reader.take("source");
        const std::string source = source_record.str(0);
        const bool display = source_record.boolean(1);
        const golden::Record& fonts_record = reader.take("fonts");
        REQUIRE_EQ(std::string(fonts_record.raw(0)), std::string("real"));
        REQUIRE_EQ(std::string(fonts_record.raw(1)), std::string("real"));
        const Scaled base_size = fonts_record.i64(2);
        const Scaled shortfall = reader.take("delimiter_shortfall").i64(0);
        const Style style = math_golden::style(reader.take("style").raw(0));

        // The list the reference parsed, as lines; then on to what it made.
        const size_t list_start = reader.position();
        MathList unused;
        (void)math_golden::read_mlist(reader, context, unused);
        const std::vector<std::string> expected_list = reader.lines(list_start, reader.position());
        reader.take("sizes");
        (void)core_golden::read_measure(reader, context);
        math_golden::RecordedFonts recorded("real");
        recorded.read(reader, context, params);
        math_golden::LibraryFonts from_library(library);
        math_golden::Fonts& fonts_source = recorded_fonts
                                               ? static_cast<math_golden::Fonts&>(recorded)
                                               : static_cast<math_golden::Fonts&>(from_library);

        MathFonts fonts;
        REQUIRE(fonts.init(base_size, fonts_source.math(), fonts_source.text()));
        fonts.delimiter_shortfall = shortfall;

        Formula formula;
        Refusal refusal;
        Node box;
        const size_t boxes_before = context.arena.box_count();
        const bool ok = typeset(source, display, fonts, context.arena, formula, box, refusal);
        CHECK(formula.style() == style);
        // Parsed either way: every source of this file is in the subset.
        CHECK_LINES_EQ(expected_list,
                       math_golden::mlist_lines(context, formula.list, formula.view()),
                       "the math list");

        if (reader.next_is("refused")) {
            ++refusals;
            reader.take();
            reader.take("message");
            const golden::Record& r = reader.take("typeset_refusal");
            if (ok) {
                FAIL("set what the reference refused (%s)", r.str(3).c_str());
                continue;
            }
            CHECK_EQ(std::string(r.raw(0)), std::string(refusal_kind_value(refusal.kind)));
            CHECK_EQ(r.i64(1), static_cast<int64_t>(refusal.position));
            CHECK_EQ(r.i64(2), static_cast<int64_t>(refusal.byte_position));
            CHECK_EQ(r.str(3), refusal.reason);
            CHECK_EQ(context.arena.box_count(), boxes_before);
        } else {
            ++set;
            if (!ok) {
                FAIL("refused what the reference set: %s", line(refusal).c_str());
                continue;
            }
            (void)core_golden::read_nodes(reader, context);
            const size_t start = reader.position();
            const core_golden::Packed packed = core_golden::read_packed(reader, context);
            // The box record alone: `packed` is hpack's report, which
            // set_formula does not keep.
            CHECK_LINES_EQ(reader.lines(start + 1, reader.position()),
                           core_golden::node_lines(context, box), "the formula's box");
            (void)packed;
            const Box& set_box = context.arena.box(box);
            const golden::Record& origin = reader.take("origin");
            CHECK_EQ(origin.i64(1), set_box.height);
            std::vector<DrawCmd> cmds;
            CHECK(ship(context.arena, box, Point{0, set_box.height}, fonts.measure(), cmds) ==
                  ShipError::NONE);
            const size_t cmds_start = reader.position();
            (void)core_golden::read_cmds(reader, context);
            CHECK_LINES_EQ(reader.lines(cmds_start, reader.position()),
                           math_golden::cmd_lines(context, cmds), "the shipped commands");
        }
        if (recorded_fonts && recorded.misses != 0)
            FAIL("%ld question(s) the reference did not ask; the first: %s", recorded.misses,
                 recorded.first_miss.c_str());
    }
    std::printf("    formulas.golden, %s fonts: %ld set, %ld refused, %ld built by hand\n",
                recorded_fonts ? "recorded" : "library", set, refusals, by_hand);
    // Exact, all three: a case that stopped being replayed would otherwise
    // only move from one count to another.
    CHECK_EQ(set, 1950L);
    CHECK_EQ(refusals, 21L);
    CHECK_EQ(by_hand, 66L);
}

} // namespace

TEST(sources_typeset_to_the_references_boxes_with_recorded_fonts)
{
    replay_formulas(true);
}

TEST(sources_typeset_to_the_references_boxes_with_the_font_library)
{
    replay_formulas(false);
}

// --- What typeset says when the words cannot be set ---------------------------------
//
// No golden holds these. The expected reasons and positions are the
// reference's, asked of it directly with the same fonts.

TEST(a_word_with_a_character_its_face_lacks_is_a_font_refusal_at_that_character)
{
    FontLibrary library;
    REQUIRE(math_golden::register_reference_fonts(library));
    math_golden::LibraryFonts source(library);
    MathFonts fonts;
    REQUIRE(fonts.init(sp_from_px(16.0), source.math(), source.text()));
    NodeArena arena;
    const auto refusal_of = [&](std::string_view text) {
        Formula formula;
        Refusal refusal;
        Node box;
        if (typeset(text, false, fonts, arena, formula, box, refusal))
            return std::string("<set>");
        return line(refusal);
    };
    // U+901F is not in Roboto. The position is the first place the character
    // occurs in the source: 10 codepoints in, 10 bytes in.
    CHECK_EQ(refusal_of("v = \\text{\xE9\x80\x9F\xE5\xBA\xA6}"),
             std::string("font 10 10 the prose face has no glyph for '\xE9\x80\x9F' (U+901F), so "
                         "its words cannot be set"));
    CHECK_EQ(refusal_of("\\text{a \xE9\x80\x9F b \xE9\x80\x9F}"),
             std::string("font 8 8 the prose face has no glyph for '\xE9\x80\x9F' (U+901F), so "
                         "its words cannot be set"));
    CHECK_EQ(refusal_of("\\textbf{ if \xE9\x80\x9F}"),
             std::string("font 12 12 the prose-bold face has no glyph for '\xE9\x80\x9F' "
                         "(U+901F), so its words cannot be set"));
    CHECK_EQ(refusal_of("\\text{\xF0\x9F\x98\x80}"),
             std::string("font 6 6 the prose face has no glyph for '\xF0\x9F\x98\x80' (U+1F600), "
                         "so its words cannot be set"));
    CHECK_EQ(arena.box_count(), size_t(0));

    // A formula the fonts do set, for contrast, with the reference's box.
    Formula formula;
    Refusal refusal;
    Node box;
    REQUIRE(typeset("\\sqrt{\\frac{\\frac{\\frac{\\frac{a}{b}}{c}}{d}}{e}}", false, fonts, arena,
                    formula, box, refusal));
    CHECK_EQ(arena.box(box).width, 1046877);
    CHECK_EQ(arena.box(box).height, 1578685);
    CHECK_EQ(arena.box(box).depth, 347404);

    // Where codepoints and bytes part: the fake prose face has everything
    // below U+0250 and nothing above.
    math_golden::FakeFonts fake("fake", "fake");
    MathFonts fake_fonts;
    REQUIRE(fake_fonts.init(180, fake.math(), fake.text()));
    Formula f2;
    Node b2;
    REQUIRE(!typeset("\\text{caf\xC3\xA9 \xCE\x94}", false, fake_fonts, arena, f2, b2, refusal));
    CHECK_EQ(line(refusal), std::string("font 11 12 the prose face has no glyph for '\xCE\x94' "
                                        "(U+0394), so its words cannot be set"));
}

TEST(words_with_no_prose_face_to_set_them_in_are_a_font_refusal)
{
    math_golden::FakeFonts none("fake", "none");
    MathFonts fonts;
    REQUIRE(fonts.init(180, none.math(), none.text()));
    NodeArena arena;
    Formula formula;
    Refusal refusal;
    Node box;
    REQUIRE(!typeset("x \\text{ if } y", false, fonts, arena, formula, box, refusal));
    CHECK_EQ(line(refusal),
             std::string("font 0 0 there is no prose face to set its words in: ' if ' is prose, "
                         "and these MathFonts were built without `text`"));
    // Python's repr switches to double quotes around a single quote, and
    // escapes one only when both kinds are present.
    REQUIRE(!typeset("\\text{it's}", false, fonts, arena, formula, box, refusal));
    CHECK_EQ(line(refusal),
             std::string("font 0 0 there is no prose face to set its words in: \"it's\" is "
                         "prose, and these MathFonts were built without `text`"));
    REQUIRE(!typeset("\\textit{it's \"q\"}", false, fonts, arena, formula, box, refusal));
    CHECK_EQ(line(refusal),
             std::string("font 0 0 there is no prose face to set its words in: 'it\\'s \"q\"' is "
                         "prose, and these MathFonts were built without `text`"));

    // A resolver that does not know the face. The reference's reason ends with
    // whatever its resolver raised ("... did not resolve: no file for
    // prose@789381"); a resolver here answers null and has no words.
    math_golden::FakeFonts broken("fake", "broken");
    MathFonts unresolved;
    REQUIRE(unresolved.init(180, broken.math(), broken.text()));
    REQUIRE(!typeset("\\textbf{b c}", false, unresolved, arena, formula, box, refusal));
    CHECK_EQ(line(refusal), std::string("font 0 0 there is no prose face to set its words in: "
                                        "prose-bold did not resolve"));
    CHECK_EQ(arena.box_count(), size_t(0));
}

TEST(a_layout_refusal_names_the_delimiter_and_a_proportion)
{
    // The chained fake at 18000: the last parenthesis and the last surd are
    // 90900 tall.
    math_golden::FakeFonts chained("chained", "none");
    MathFonts fonts;
    REQUIRE(fonts.init(18000, chained.math(), chained.text()));
    fonts.delimiter_shortfall = 9000;
    NodeArena arena;
    Formula formula;
    Refusal refusal;
    Node box;
    MathStatus status;
    status.error = MathError::DELIMITER_TOO_SMALL;
    status.codepoint = SURD;
    status.needed = 90901;
    status.largest = 90900;
    CHECK_EQ(line(refusal_from(status, "", formula, fonts)),
             std::string("layout 0 0 what is under the root needs a radical sign 1.00 times as "
                         "tall as the largest the math face has, and one is not assembled from "
                         "pieces here"));
    status.codepoint = 0x27E8;
    status.needed = 3;
    status.largest = 2;
    CHECK_EQ(line(refusal_from(status, "", formula, fonts)),
             std::string("layout 0 0 what is between the delimiters needs a \xE2\x9F\xA8 1.50 "
                         "times as tall as the largest the math face has, and one is not "
                         "assembled from pieces here"));
    // Nothing measured at all: the engine's own message, as the reference
    // gives it.
    status.codepoint = '(';
    status.needed = 500;
    status.largest = 0;
    CHECK_EQ(line(refusal_from(status, "", formula, fonts)),
             std::string("layout 0 0 the math engine will not set it: U+0028 has no variant 500 "
                         "sp tall (the largest is 0 sp), and delimiters are not assembled from "
                         "pieces here"));
    // Anything else the engine can say is a FONT matter, as the reference's
    // catch-all makes it; the parser emits no list that reaches these.
    status = MathStatus();
    status.error = MathError::NOT_IMPLEMENTED;
    const Refusal other = refusal_from(status, "", formula, fonts);
    CHECK(other.kind == RefusalKind::FONT);
    CHECK_EQ(other.position, size_t(0));
    CHECK(other.reason.rfind("the math face could not set it: ", 0) == 0);
    (void)arena;
    (void)box;
}

// --- Hostile input -------------------------------------------------------------------

TEST(the_references_hostile_sources_at_their_real_sizes)
{
    // tests/test_mathtext.py's hostile-size table: each returns, and returns
    // what the reference returns. Nesting is refused at MAX_DEPTH + 1, so no
    // source can make the parser, or the engine after it, recurse deeper.
    CHECK_EQ(refused(repeated("{", 50000)),
             std::string("nesting 24 24 nested more than 24 levels deep"));
    CHECK_EQ(refused(repeated("}", 50000)), std::string("syntax 0 0 a } with no { before it"));
    CHECK_EQ(refused(repeated("x^", 50000)),
             std::string("syntax 3 3 a second superscript on one atom"));
    CHECK_EQ(refused(repeated("{x^", 50000)),
             std::string("nesting 72 72 nested more than 24 levels deep"));
    CHECK_EQ(refused(repeated("\\frac", 50000)),
             std::string("nesting 125 125 nested more than 24 levels deep"));
    CHECK_EQ(refused(repeated("\\overline", 50000)),
             std::string("nesting 225 225 nested more than 24 levels deep"));
    CHECK_EQ(refused(repeated("_{", 50000)),
             std::string("nesting 49 49 nested more than 24 levels deep"));
    CHECK_EQ(refused(repeated("\\", 200000)),
             std::string("alignment 0 0 \\\\ breaks a line or a column, and nothing here is "
                         "aligned"));
    CHECK_EQ(refused(repeated("\\left(", 50000)),
             std::string("nesting 144 144 nested more than 24 levels deep"));
    CHECK_EQ(refused(repeated("\\right)", 50000)),
             std::string("syntax 0 0 a \\right with no \\left before it"));
    CHECK_EQ(refused(repeated("\\sqrt", 50000)),
             std::string("nesting 125 125 nested more than 24 levels deep"));
    CHECK_EQ(refused(repeated("\\sqrt{", 50000)),
             std::string("nesting 149 149 nested more than 24 levels deep"));
    CHECK_EQ(refused(repeated("{x}", 60000) + "^"),
             std::string("syntax 180000 180000 ^ has nothing after it"));
    CHECK_EQ(refused("\\mathrm{" + repeated("a", 200000)),
             std::string("syntax 7 7 a { that is never closed"));
    CHECK_EQ(refused("\\begin" + repeated(" ", 200000)),
             std::string("environment 0 0 \\begin opens or closes an environment, and there are "
                         "none here"));
    CHECK_EQ(refused("\\left" + repeated(" ", 200000) + "("),
             std::string("syntax 0 0 a \\left with no \\right after it"));
    CHECK_EQ(refused("\\text{" + repeated("a ", 100000)),
             std::string("syntax 5 5 a { that is never closed"));

    // The ones that parse: how many items, and what the first is.
    {
        Parsed p("x" + repeated("'", 200000));
        REQUIRE(p.ok);
        REQUIRE_EQ(p.formula.view().size, size_t(1));
        MathView primes;
        REQUIRE(p.formula.list.view(p.formula.view()[0].sup.span, &primes));
        CHECK_EQ(primes.size, size_t(200000));
        CHECK_EQ(primes[199999].nucleus.codepoint, 0x2032u);
    }
    {
        Parsed p("x" + repeated("\\,", 200000));
        REQUIRE(p.ok);
        CHECK_EQ(p.formula.view().size, size_t(200001));
    }
    {
        Parsed p(repeated("\\,", 200000));
        REQUIRE(p.ok);
        CHECK_EQ(p.formula.view().size, size_t(200000));
        CHECK(p.formula.view()[0].kind == AtomType::SPACE);
    }
    {
        Parsed p(repeated("x", 200000));
        REQUIRE(p.ok);
        CHECK_EQ(p.formula.view().size, size_t(200000));
    }
    {
        Parsed p(repeated("{}", 100000));
        REQUIRE(p.ok);
        CHECK_EQ(p.formula.view().size, size_t(100000));
        CHECK(p.formula.view()[0].nucleus.type == FieldType::SUB_LIST);
    }
    {
        Parsed p("\\mathrm{" + repeated("a", 200000) + "}");
        REQUIRE(p.ok);
        REQUIRE_EQ(p.formula.view().size, size_t(1));
        CHECK_EQ(p.formula.view()[0].nucleus.span.count, 200000u);
    }
    {
        Parsed p(repeated(" ", 200000) + "x");
        REQUIRE(p.ok);
        CHECK_EQ(p.formula.view().size, size_t(1));
    }
    {
        Parsed p("\\sum" + repeated("\\limits", 60000));
        REQUIRE(p.ok);
        REQUIRE_EQ(p.formula.view().size, size_t(1));
        CHECK(p.formula.view()[0].kind == AtomType::OP);
        CHECK(p.formula.view()[0].limits == Limits::LIMITS);
    }
    {
        Parsed p(repeated("\\left( x \\right)", 30000));
        REQUIRE(p.ok);
        CHECK_EQ(p.formula.view().size, size_t(30000));
        CHECK(p.formula.view()[0].kind == AtomType::INNER);
    }
    {
        Parsed p("\\text{" + repeated("a ", 100000) + "}");
        REQUIRE(p.ok);
        REQUIRE_EQ(p.formula.view().size, size_t(1));
        CHECK(p.formula.view()[0].nucleus.type == FieldType::MATH_TEXT);
        CHECK_EQ(p.formula.view()[0].nucleus.span.count, 200000u);
    }
    {
        Parsed p(repeated("\\quad", 60000));
        REQUIRE(p.ok);
        CHECK_EQ(p.formula.view().size, size_t(60000));
    }
}

TEST(arbitrary_sources_always_get_an_answer_and_what_parses_can_be_converted)
{
    // Not the reference's corpus: strings made from the pieces that steer the
    // parser, mixed with raw bytes, from a fixed generator: under 24 pieces
    // each, and one in 32 of up to 2,000 pieces that go together more often
    // than not, so that long lists reach the engine too. The claim is only
    // that every one is answered, with a position inside the source, and that
    // a list the parser lets through is one the engine sets or refuses.
    static const char* const pieces[] = {
        "\\frac",      "{",
        "}",           "^",
        "_",           "\\left",
        "\\right",     "(",
        ")",           "\\sqrt",
        "\\text{",     "x",
        " ",           "\\",
        "'",           "\\limits",
        "\\sum",       "\xFF",
        "\xC3",        "\xA9",
        "$",           "#",
        "&",           "~",
        "\\binom",     "\\begin",
        "{matrix}",    "*",
        "[",           ".",
        "|",           "\\mathbb{",
        "A",           "1",
        "\\,",         "\\ ",
        "\\int",       "\\nolimits",
        "\\overline",  "\xE2\x88\x91",
        "\\mathrm{",   "\\operatorname{",
        "\\underline", "<",
        ">",           "\\{",
        "\\}",         "\xF0\x9D\x90\x80",
    };
    const size_t piece_count = sizeof pieces / sizeof pieces[0];
    static const char* const long_pieces[] = {
        "x",     "A",          "1",        " ",           "\\,",
        "\\ ",   "{x}",        "\\frac12", "\\sqrt x",    "{x^2}",
        "{x_1}", "\\sum ",     "\\int ",   "(",           ")",
        "<",     ">",          "*",        ".",           "\\{",
        "\\}",   "\\text{ab}", "'",        "\\mathbb{R}", "\\left(x\\right)",
    };
    const size_t long_piece_count = sizeof long_pieces / sizeof long_pieces[0];
    math_golden::FakeFonts fake("chained", "fake");
    MathFonts fonts;
    REQUIRE(fonts.init(18000, fake.math(), fake.text()));
    NodeArena arena;
    Formula formula;
    Refusal refusal;
    uint64_t state = 0x9E3779B97F4A7C15ull;
    const auto next = [&state]() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(state >> 33);
    };
    long parsed_count = 0;
    long set_count = 0;
    long long_parsed_count = 0;
    for (int round = 0; round < 30000; ++round) {
        std::string source;
        const bool is_long = round % 32 == 31;
        const uint32_t length = is_long ? next() % 2000 : next() % 24;
        for (uint32_t i = 0; i < length; ++i) {
            if (is_long)
                source += long_pieces[next() % long_piece_count];
            else if (next() % 16 == 0)
                source += static_cast<char>(next() & 0xFF);
            else
                source += pieces[next() % piece_count];
        }
        const bool display = (next() & 1) != 0;
        if (!parse(source, display, formula, refusal)) {
            CHECK(!refusal.reason.empty());
            CHECK(refusal.byte_position <= source.size());
            CHECK(refusal.position <= refusal.byte_position);
            CHECK(refusal.kind != RefusalKind::FONT && refusal.kind != RefusalKind::LAYOUT);
            continue;
        }
        ++parsed_count;
        long_parsed_count += is_long ? 1 : 0;
        CHECK(!formula.view().empty());
        const NodeArena::Mark mark = arena.mark();
        Node box;
        MathStatus status;
        if (set_formula(formula, fonts, arena, box, status)) {
            ++set_count;
        } else {
            // What the parser emits the engine never calls malformed.
            CHECK(status.is_layout_refusal());
            const Refusal why = refusal_from(status, source, formula, fonts);
            CHECK(!why.reason.empty());
            CHECK(why.byte_position <= source.size());
        }
        arena.rewind(mark);
    }
    std::printf("    30000 sources: %ld parsed (%ld of the 937 long ones), %ld of those set\n",
                parsed_count, long_parsed_count, set_count);
    CHECK(parsed_count > 1000);
    CHECK(long_parsed_count > 400);
    CHECK(set_count > 500);
}

TEST(the_deepest_nesting_the_parser_allows_still_sets)
{
    // Braces, each a group holding a script, so no level collapses into the
    // one above, and a last script that is one atom, which is a level too:
    // MAX_DEPTH in all, the deepest list the parser hands the engine.
    std::string source;
    for (int i = 0; i < MAX_DEPTH - 1; ++i)
        source += "{x^";
    source += "y";
    for (int i = 0; i < MAX_DEPTH - 1; ++i)
        source += "}";
    Parsed parsed(source);
    REQUIRE(parsed.ok);
    math_golden::FakeFonts fake("fake", "none");
    MathFonts fonts;
    REQUIRE(fonts.init(180, fake.math(), fake.text()));
    NodeArena arena;
    Node box;
    MathStatus status;
    CHECK(set_formula(parsed.formula, fonts, arena, box, status));
    CHECK(status.ok());
    // One more level is the parser's to refuse.
    CHECK_EQ(refused("{x^" + source + "}"),
             std::string("nesting 72 72 nested more than 24 levels deep"));
}

TEST(the_deepest_list_the_parser_can_emit_is_inside_the_engines_bound)
{
    // A \frac taken as the argument of a \frac is one level of the parser's
    // nesting and two of the list's: an Inner atom holding a list holding the
    // fraction, whose numerator is a list again. MAX_DEPTH of them, with an
    // operator name innermost, whose letters are a list of their own, is the
    // deepest list found that `parse` produces, and the engine must not
    // answer TOO_DEEP for something the reference sets.
    std::string source = repeated("\\frac", MAX_DEPTH);
    source += " \\sin";
    source += repeated(" x", MAX_DEPTH);
    Parsed parsed(source);
    REQUIRE(parsed.ok);
    const int depth = list_depth(parsed.formula.list, parsed.formula.view());
    CHECK_EQ(depth, 2 * MAX_DEPTH + 2);
    CHECK(depth <= MAX_MATH_NESTING);
    // With a letter innermost it is one less.
    {
        Parsed plain(repeated("\\frac", MAX_DEPTH) + repeated(" x", MAX_DEPTH + 1));
        REQUIRE(plain.ok);
        CHECK_EQ(list_depth(plain.formula.list, plain.formula.view()), 2 * MAX_DEPTH + 1);
    }

    math_golden::FakeFonts fake("fake", "none");
    MathFonts fonts;
    REQUIRE(fonts.init(18000, fake.math(), fake.text()));
    NodeArena arena;
    Node box;
    MathStatus status;
    CHECK(set_formula(parsed.formula, fonts, arena, box, status));
    CHECK(status.ok());
    // One more \frac is the parser's to refuse, at the argument that would be
    // the twenty-fifth level.
    Parsed deeper("\\frac" + source + " x");
    CHECK(!deeper.ok);
    CHECK(deeper.refusal.kind == RefusalKind::NESTING);
    // Past the twenty-five commands and the blank after them.
    CHECK_EQ(deeper.refusal.position, size_t(5 * (MAX_DEPTH + 1) + 1));
}

TEST(no_list_the_parser_lets_through_is_deeper_than_the_stated_bound)
{
    // Every way of going down a level, repeated to the parser's limit and one
    // past it with each of several things innermost: whatever parses nests no
    // deeper than the header says.
    const char* const openers[] = {"\\frac ", "{x^",      "\\sqrt ", "\\overline ", "_{",
                                   "\\left(", "\\binom ", "\\text{", "\\mathrm{"};
    const char* const innermost[] = {
        "x",          "\\sin",     "\\frac12",         "\\text{ab}",
        "x'",         "\\sum_a^b", "\\left(x\\right)", "\\operatorname{ab}",
        "\\mathbb{R}"};
    int deepest = 0;
    long parsed_count = 0;
    for (const char* opener : openers) {
        for (const char* inner : innermost) {
            for (int levels = MAX_DEPTH - 2; levels <= MAX_DEPTH + 1; ++levels) {
                for (const char* closer : {"", "}", " x", "\\right)", " x x"}) {
                    const std::string source = repeated(opener, static_cast<size_t>(levels)) +
                                               inner +
                                               repeated(closer, static_cast<size_t>(levels) + 1);
                    Parsed parsed(source);
                    if (!parsed.ok)
                        continue;
                    ++parsed_count;
                    const int depth = list_depth(parsed.formula.list, parsed.formula.view());
                    deepest = depth > deepest ? depth : deepest;
                    if (depth > 2 * MAX_DEPTH + 2) {
                        CHECK_CONTEXT("%s", source.c_str());
                        CHECK(depth <= 2 * MAX_DEPTH + 2);
                    }
                }
            }
        }
    }
    std::printf("    %ld parsed; the deepest list is %d levels\n", parsed_count, deepest);
    CHECK(parsed_count > 20);
    CHECK(deepest <= 2 * MAX_DEPTH + 2);
}

TEST(a_source_that_is_not_utf8_is_parsed_as_the_replacement_characters_it_decodes_to)
{
    // A stray continuation byte, a truncated sequence, an overlong form, an
    // encoded surrogate: each is U+FFFD, which is printable and not in the
    // subset.
    for (const char* bad :
         {"\x80", "x\xE2\x82", "\xC0\xAF", "\xED\xA0\x80", "\xFF", "\xF4\x90\x80\x80"}) {
        CHECK_CONTEXT("source of %zu bytes", std::strlen(bad));
        Parsed parsed(bad);
        CHECK(!parsed.ok);
        CHECK(parsed.refusal.kind == RefusalKind::CHARACTER);
        CHECK_EQ(parsed.refusal.reason,
                 std::string("the character '\xEF\xBF\xBD' (U+FFFD) is not in the subset"));
        CHECK(parsed.refusal.byte_position <= std::strlen(bad));
    }
    // After a well-formed prefix the position is where the bad bytes start.
    CHECK_EQ(refused("ab\xFFzz"),
             std::string("character 2 2 the character '\xEF\xBF\xBD' (U+FFFD) is not in the "
                         "subset"));
    CHECK_EQ(refused("\xC3\xA9"),
             std::string("character 0 0 the character '\xC3\xA9' (U+00E9) is not in the subset"));
    // In \text the replacement character is a word like any other.
    Parsed worded("\\text{a\xFF"
                  "b}");
    REQUIRE(worded.ok);
    std::string_view words;
    REQUIRE(worded.formula.list.text_of(worded.formula.view()[0].nucleus.span, &words));
    CHECK_EQ(std::string(words), std::string("a\xEF\xBF\xBD"
                                             "b"));
    // As a control symbol it is an unknown command, named as it decodes.
    CHECK_EQ(refused("\\\xFF"),
             std::string("unknown-command 0 0 \\\xEF\xBF\xBD is not in the subset"));
}

TEST(a_nul_is_a_character_and_does_not_end_the_source)
{
    const std::string source("x\0y", 3);
    Parsed parsed(source);
    CHECK(!parsed.ok);
    CHECK_EQ(line(parsed.refusal),
             std::string("character 1 1 the character '\\x00' (U+0000) is not in the subset"));
    // After a backslash it is a control symbol nobody defined.
    const std::string control("\\\0", 2);
    Parsed unknown(control);
    CHECK(!unknown.ok);
    CHECK(unknown.refusal.kind == RefusalKind::UNKNOWN_COMMAND);
    CHECK_EQ(unknown.refusal.reason, std::string("\\\0 is not in the subset", 23));
    // And in \text it cannot be printed.
    const std::string worded("\\text{a\0}", 9);
    CHECK_EQ(refused(worded),
             std::string("character 7 7 the character U+0000 in \\text cannot be printed"));
}

TEST(positions_are_given_in_codepoints_and_in_bytes)
{
    // U+00E9 is two bytes and U+1D400 four; the refusal is at the '#'.
    Parsed parsed("\\text{\xC3\xA9\xF0\x9D\x90\x80} #");
    REQUIRE(!parsed.ok);
    CHECK(parsed.refusal.kind == RefusalKind::MACRO);
    CHECK_EQ(parsed.refusal.position, size_t(10));
    CHECK_EQ(parsed.refusal.byte_position, size_t(14));
    // At the very end of the source both are its length.
    Parsed open("\\text{\xC3\xA9} {");
    REQUIRE(!open.ok);
    CHECK_EQ(line(open.refusal), std::string("syntax 9 10 a { that is never closed"));
    Parsed dangling("\xC3\x97\\");
    REQUIRE(!dangling.ok);
    CHECK_EQ(line(dangling.refusal), std::string("syntax 1 2 a backslash with nothing after it"));
}

TEST(a_formula_is_reused_and_a_refusal_leaves_it_empty)
{
    Formula formula;
    Refusal refusal;
    REQUIRE(parse("\\frac{a}{b} + \\text{if}", true, formula, refusal));
    const size_t first = formula.list.item_count();
    CHECK(first > 3);
    CHECK(formula.display);
    CHECK(formula.style() == Style::DISPLAY);
    REQUIRE(parse("x", false, formula, refusal));
    CHECK_EQ(formula.list.item_count(), size_t(1));
    CHECK_EQ(formula.view().size, size_t(1));
    CHECK(formula.style() == Style::TEXT);
    REQUIRE(!parse("x^", false, formula, refusal));
    CHECK_EQ(formula.list.item_count(), size_t(0));
    CHECK(formula.view().empty());
    CHECK(refusal.kind == RefusalKind::SYNTAX);
    // And a success clears the refusal.
    REQUIRE(parse("y", false, formula, refusal));
    CHECK(refusal.reason.empty());
    CHECK_EQ(refusal.position, size_t(0));
}
