// src/math_atoms.h and src/math_mlist: the math list and mlist_to_hlist.
//
// Replays every conversion of tests/golden/math/mlist.golden and
// formulas.golden (tools/oracle/FORMAT-math.md section 4) and compares, node
// for node and scaled point for scaled point: the hlist, its hpack, and what
// ship makes of it; or the refusal. Each case is run up to three times, with
// fonts that differ in where their numbers come from:
//
//   recorded   faces that know only the answers the reference's fonts gave
//              during that one conversion (the case's q_* records). No font
//              reader is involved, and a question the reference did not ask
//              is a failure.
//   fake       the reference tests' fake fonts, restated natively.
//   library    src/fontlib over tests/data/fonts: the port's own reader.
//
// Then the tables of style.golden that are this module's (the style algebra,
// the spacing table, half, math_glue, the three sizes), then what is the
// port's own: the outcomes it defines for a list the reference cannot be
// handed, and a handful of the reference's tests/test_mathlist.py restated as
// properties, so that a reader can see a claim and not only a diff.

#include "math_golden.h"

#include "math_atoms.h"
#include "math_mlist.h"
#include "pack.h"
#include "shipout.h"

#include <cmath>
#include <cstdlib>
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

// Installs a contract handler for a scope, so the path after a violation can
// be run where assertions are live.
struct ExpectViolations {
    ExpectViolations()
        : _previous(contract_handler())
    {
        g_violations = 0;
        contract_handler() = &count_violation;
    }
    ~ExpectViolations() { contract_handler() = _previous; }

    ContractHandler _previous;
};

enum class FontMode { RECORDED, FAKE, LIBRARY };

const char* mode_name(FontMode mode)
{
    switch (mode) {
    case FontMode::RECORDED:
        return "recorded";
    case FontMode::FAKE:
        return "fake";
    case FontMode::LIBRARY:
        return "library";
    }
    return "?";
}

struct Tally {
    long cases = 0;
    long set = 0;
    long refused = 0;
};

// One `mlist` case under one kind of fonts. Returns false if the case does not
// apply to that kind.
bool run_mlist_case(const golden::File& file, const core_golden::Case& c,
                    const math_golden::ParamsTable& params, FontMode mode, FontLibrary* library,
                    Tally& tally)
{
    core_golden::Reader reader(file, c);
    core_golden::Context context;
    MathList list;

    if (reader.next_is("source"))
        reader.take();
    const golden::Record& fonts_record = reader.take("fonts");
    const std::string math_mode(fonts_record.raw(0));
    const std::string text_mode(fonts_record.raw(1));
    const Scaled base_size = fonts_record.i64(2);
    if (mode == FontMode::FAKE && (math_mode == "real" || text_mode == "real"))
        return false;
    if (mode == FontMode::LIBRARY && (math_mode != "real" || text_mode != "real"))
        return false;

    const Scaled shortfall = reader.take("delimiter_shortfall").i64(0);
    const Style style = math_golden::style(reader.take("style").raw(0));
    const std::vector<MathItem> items = math_golden::read_mlist(reader, context, list);
    const golden::Record& sizes = reader.take("sizes");
    const core_golden::MeasureTable measures = core_golden::read_measure(reader, context);
    math_golden::RecordedFonts recorded(text_mode);
    recorded.read(reader, context, params);

    math_golden::FakeFonts fake(math_mode, text_mode);
    math_golden::LibraryFonts from_library(*library);
    math_golden::Fonts* source = &recorded;
    if (mode == FontMode::FAKE)
        source = &fake;
    else if (mode == FontMode::LIBRARY)
        source = &from_library;

    MathFonts fonts;
    std::string why;
    if (!fonts.init(base_size, source->math(), source->text(), &why)) {
        FAIL("MathFonts::init refused: %s", why.c_str());
        return true;
    }
    fonts.delimiter_shortfall = shortfall;
    ++tally.cases;

    // `sizes`: the three sizes and their keys.
    {
        golden::Cursor cursor(sizes);
        for (const MathSize size : {MathSize::TEXT, MathSize::SCRIPT, MathSize::SCRIPT_SCRIPT}) {
            CHECK_EQ(cursor.i64(), fonts.size(size));
            CHECK_EQ(cursor.str(), context.faces.text(fonts.key(size)));
        }
        cursor.expect_done();
    }

    const size_t boxes_before = context.arena.box_count();
    const size_t nodes_before = context.arena.node_count();
    std::vector<Node> hlist;
    MathStatus status = mlist_to_hlist(list, items, style, fonts, context.arena, hlist);

    if (reader.next_is("refused")) {
        ++tally.refused;
        const golden::Record& refused = reader.take();
        // The reference's convert() packs inside the same try; a list that set
        // but cannot be measured is refused there.
        if (status.ok()) {
            (void)hpack(context.arena, hlist, 0, PackMode::ADDITIONAL, fonts.measure());
            if (fonts.unmeasured())
                status.error = MathError::TEXT_FACE_UNAVAILABLE;
        }
        CHECK_EQ(std::string(refused.raw(0)), std::string(math_golden::slug(status.error)));
        if (status.error == MathError::DELIMITER_TOO_SMALL && refused.size() == 4) {
            CHECK_EQ(refused.u32(1), status.codepoint);
            CHECK_EQ(refused.i64(2), status.needed);
            CHECK_EQ(refused.i64(3), status.largest);
        }
        if (status.error == MathError::MISSING_GLYPH && refused.size() == 3) {
            CHECK_EQ(refused.u32(1), status.codepoint);
            CHECK_EQ(refused.str(2), std::string(face_value(status.face)));
        }
        if (!status.ok() && reader.next_is("message")) {
            const std::string message = reader.take().str(0);
            if (status.error == MathError::DELIMITER_TOO_SMALL ||
                status.error == MathError::MISSING_GLYPH)
                CHECK_EQ(message, math_error_message(status));
        }
        if (status.error != MathError::NONE && status.error != MathError::TEXT_FACE_UNAVAILABLE) {
            // Nothing is left behind by a conversion that failed.
            CHECK(hlist.empty());
            CHECK_EQ(context.arena.box_count(), boxes_before);
            CHECK_EQ(context.arena.node_count(), nodes_before);
        }
        const bool layout = status.error == MathError::DELIMITER_TOO_SMALL ||
                            status.error == MathError::MISSING_GLYPH ||
                            status.error == MathError::TEXT_FACE_UNAVAILABLE;
        CHECK_EQ(status.is_layout_refusal(), layout);
    } else {
        ++tally.set;
        if (!status.ok()) {
            FAIL("the port refused (%s: %s) what the reference set",
                 math_golden::slug(status.error), math_error_message(status).c_str());
            return true;
        }

        // What mlist_to_hlist returned, unpacked.
        size_t start = reader.position();
        (void)core_golden::read_nodes(reader, context);
        std::vector<std::string> written;
        core_golden::write_nodes(context, "hlist", hlist, written);
        CHECK_LINES_EQ(reader.lines(start, reader.position()), written, "the hlist");

        // hpack at natural width.
        const PackResult packed =
            hpack(context.arena, hlist, 0, PackMode::ADDITIONAL, fonts.measure());
        CHECK(packed.error == PackError::NONE);
        const Node box = context.arena.add_box(packed.box, hlist);
        start = reader.position();
        (void)core_golden::read_packed(reader, context);
        written.clear();
        core_golden::write_packed(
            context, core_golden::PACK_FITNESS_TOKENS[static_cast<int>(packed.fitness)],
            packed.natural, packed.excess, packed.badness, packed.overfull_by, box, written);
        CHECK_LINES_EQ(reader.lines(start, reader.position()), written, "the packed box");

        // ship, with the box's top-left corner at the origin.
        const golden::Record& origin = reader.take("origin");
        CHECK_EQ(origin.i64(0), 0);
        CHECK_EQ(origin.i64(1), packed.box.height);
        std::vector<DrawCmd> cmds;
        const ShipError shipped =
            ship(context.arena, box, Point{0, packed.box.height}, fonts.measure(), cmds);
        CHECK(shipped == ShipError::NONE);
        start = reader.position();
        (void)core_golden::read_cmds(reader, context);
        CHECK_LINES_EQ(reader.lines(start, reader.position()),
                       math_golden::cmd_lines(context, cmds), "the shipped commands");
        CHECK(!fonts.unmeasured());

        // The measure the formula was packed with answers as the reference's.
        for (const core_golden::MeasureTable::Entry& entry : measures.entries) {
            const CharDimens dimens = fonts.measure(entry.font, entry.codepoint);
            CHECK_CONTEXT("measure %s %u", context.faces.text(entry.font).c_str(), entry.codepoint);
            CHECK_EQ(dimens.width, entry.width);
            CHECK_EQ(dimens.height, entry.height);
            CHECK_EQ(dimens.depth, entry.depth);
        }
    }

    if (mode == FontMode::RECORDED) {
        // A question with no recorded answer is one the reference did not ask.
        if (recorded.misses != 0)
            FAIL("%ld question(s) the reference did not ask; the first: %s", recorded.misses,
                 recorded.first_miss.c_str());
    }
    return true;
}

void replay_file(const char* name, FontMode mode, long expect_at_least)
{
    golden::File file;
    REQUIRE(file.load(golden::path(name)));
    const std::vector<core_golden::Case> cases = core_golden::cases(file);
    REQUIRE(!cases.empty());

    FontLibrary library;
    if (mode == FontMode::LIBRARY)
        REQUIRE(math_golden::register_reference_fonts(library));

    math_golden::ParamsTable params;
    Tally tally;
    for (const core_golden::Case& c : cases) {
        CHECK_CONTEXT("%s: case %s (line %d), %s fonts", name, c.name.c_str(), c.line,
                      mode_name(mode));
        if (c.kind == "table") {
            if (c.name == "font-params") {
                core_golden::Context context;
                params.read(file, c, context);
            }
            continue;
        }
        if (c.kind != "mlist") {
            FAIL("a case kind this test does not know: %s", c.kind.c_str());
            continue;
        }
        run_mlist_case(file, c, params, mode, &library, tally);
    }
    std::printf("    %s, %s fonts: %ld cases, %ld set, %ld refused\n", name, mode_name(mode),
                tally.cases, tally.set, tally.refused);
    // A filter that silently matched nothing must not read as a pass.
    CHECK(tally.cases >= expect_at_least);
    CHECK_EQ(tally.cases, tally.set + tally.refused);
}

// --- Small helpers for the tests that build lists by hand ---------------------------

struct Bench {
    math_golden::FakeFonts source;
    MathFonts fonts;
    MathList list;
    NodeArena arena;

    explicit Bench(const char* math_mode = "fake", const char* text_mode = "none",
                   Scaled base = 180)
        : source(math_mode, text_mode)
    {
        std::string why;
        if (!fonts.init(base, source.math(), source.text(), &why))
            FAIL("init: %s", why.c_str());
    }

    MathStatus convert(const std::vector<MathItem>& items, Style style, std::vector<Node>& out)
    {
        return mlist_to_hlist(list, items, style, fonts, arena, out);
    }

    std::vector<Node> set(const std::vector<MathItem>& items, Style style)
    {
        std::vector<Node> out;
        const MathStatus status = convert(items, style, out);
        if (!status.ok())
            FAIL("refused: %s", math_error_message(status).c_str());
        return out;
    }
};

MathItem ord_(uint32_t c, const Field& sup = Field(), const Field& sub = Field())
{
    return MathItem::atom(AtomType::ORD, Field::math_char(c), sup, sub);
}

MathItem of(AtomType kind, uint32_t c)
{
    return MathItem::atom(kind, Field::math_char(c));
}

std::vector<Scaled> glue_widths(NodeView nodes)
{
    std::vector<Scaled> widths;
    for (const Node& node : nodes) {
        if (node.is(NodeType::GLUE))
            widths.push_back(node.width());
    }
    return widths;
}

std::vector<Node> boxes_of(NodeView nodes)
{
    std::vector<Node> boxes;
    for (const Node& node : nodes) {
        if (node.is(NodeType::BOX))
            boxes.push_back(node);
    }
    return boxes;
}

// Every glyph in a list, in reading order, however deeply nested.
void codepoints(const NodeArena& arena, NodeView nodes, std::vector<uint32_t>& out)
{
    for (const Node& node : nodes) {
        if (node.is(NodeType::CHAR))
            out.push_back(node.codepoint());
        else if (node.is(NodeType::BOX))
            codepoints(arena, arena.contents(node), out);
    }
}

std::vector<uint32_t> codepoints(const NodeArena& arena, NodeView nodes)
{
    std::vector<uint32_t> out;
    codepoints(arena, nodes, out);
    return out;
}

} // namespace

// --- The goldens ----------------------------------------------------------------------

TEST(mlist_golden_with_recorded_fonts)
{
    replay_file("math/mlist.golden", FontMode::RECORDED, 2589);
}

TEST(mlist_golden_with_the_fake_fonts)
{
    replay_file("math/mlist.golden", FontMode::FAKE, 1536);
}

TEST(mlist_golden_with_the_font_library)
{
    replay_file("math/mlist.golden", FontMode::LIBRARY, 1053);
}

TEST(formulas_golden_with_recorded_fonts)
{
    replay_file("math/formulas.golden", FontMode::RECORDED, 2000);
}

TEST(formulas_golden_with_the_font_library)
{
    replay_file("math/formulas.golden", FontMode::LIBRARY, 2000);
}

// --- style.golden: the tables that are this module's --------------------------------

TEST(the_style_algebra_and_the_spacing_table_match_the_reference)
{
    golden::File file;
    REQUIRE(file.load(golden::path("math/style.golden")));
    long records = 0;
    for (const core_golden::Case& c : core_golden::cases(file)) {
        if (c.name != "style-algebra")
            continue;
        core_golden::Reader reader(file, c);
        while (!reader.done()) {
            const golden::Record& r = reader.take();
            CHECK_CONTEXT("style.golden line %d", r.line);
            golden::Cursor cursor(r);
            ++records;
            if (r.is("style")) {
                const Style s = math_golden::style(cursor.raw());
                CHECK_EQ(cursor.i64(), static_cast<int64_t>(s));
                CHECK(math_golden::style(cursor.raw()) == cramped_style(s));
                CHECK(math_golden::style(cursor.raw()) == sup_style(s));
                CHECK(math_golden::style(cursor.raw()) == sub_style(s));
                CHECK(math_golden::style(cursor.raw()) == num_style(s));
                CHECK(math_golden::style(cursor.raw()) == denom_style(s));
                CHECK(math_golden::math_size(cursor.raw()) == size_for(s));
                CHECK_EQ(cursor.boolean(), is_display(s));
                CHECK_EQ(cursor.boolean(), is_cramped(s));
            } else if (r.is("mathsize")) {
                const MathSize size = math_golden::math_size(cursor.raw());
                CHECK_EQ(cursor.i64(), static_cast<int64_t>(size));
            } else if (r.is("atomtype")) {
                const AtomType kind = math_golden::atom_type(cursor.raw());
                CHECK_EQ(cursor.i64(), spacing_class(kind));
            } else if (r.is("limits")) {
                (void)math_golden::limits(cursor.raw());
            } else if (r.is("spacing")) {
                const AtomType left = math_golden::atom_type(cursor.raw());
                const AtomType right = math_golden::atom_type(cursor.raw());
                const std::string code = cursor.str();
                REQUIRE_EQ(code.size(), size_t(1));
                CHECK_EQ(
                    static_cast<int>(code[0]),
                    static_cast<int>(MATH_SPACING[8 * spacing_class(left) + spacing_class(right)]));
            } else if (r.is("muglue_const")) {
                const std::string name(cursor.raw());
                const MuGlue expected{cursor.i64(), cursor.i64(), cursor.i64()};
                const MuGlue* ours = nullptr;
                if (name == "thin_mu" || name == "thin_space")
                    ours = &THIN_MU;
                else if (name == "med_mu" || name == "med_space")
                    ours = &MED_MU;
                else if (name == "thick_mu" || name == "thick_space")
                    ours = &THICK_MU;
                else if (name == "negative_thin_space")
                    ours = &NEGATIVE_THIN_MU;
                else if (name == "quad")
                    ours = &QUAD_MU;
                else if (name == "qquad")
                    ours = &QQUAD_MU;
                REQUIRE(ours != nullptr);
                CHECK(*ours == expected);
            } else if (r.is("const")) {
                const std::string name(cursor.raw());
                const std::string type(cursor.raw());
                if (name == "math_spacing")
                    CHECK_EQ(cursor.str(), std::string(MATH_SPACING));
                else if (name == "script_space")
                    CHECK_EQ(cursor.i64(), SCRIPT_SPACE);
                else if (name == "delimiter_factor")
                    CHECK_EQ(cursor.i64(), DELIMITER_FACTOR);
                else if (name == "delimiter_shortfall")
                    CHECK_EQ(cursor.i64(), DELIMITER_SHORTFALL);
                else if (name == "largest_variant_must_cover_numerator")
                    CHECK_EQ(cursor.i64(), LARGEST_VARIANT_MUST_COVER_NUMERATOR);
                else if (name == "largest_variant_must_cover_denominator")
                    CHECK_EQ(cursor.i64(), LARGEST_VARIANT_MUST_COVER_DENOMINATOR);
                else if (name == "null_delimiter_space_quads")
                    CHECK_EQ(cursor.f64(), NULL_DELIMITER_SPACE_QUADS);
                else if (name == "surd")
                    CHECK_EQ(cursor.i64(), static_cast<int64_t>(SURD));
                else if (name == "max_depth")
                    continue; // the formula parser's
                else
                    FAIL("a constant this test does not know: %s", name.c_str());
            } else if (r.is("half")) {
                const Scaled value = cursor.i64();
                CHECK_EQ(half(value), cursor.i64());
            } else if (r.is("from_mu")) {
                const MuGlue mu_glue{cursor.i64(), cursor.i64(), cursor.i64()};
                const Scaled mu = cursor.i64();
                const GlueSpec spec = from_mu(mu_glue, mu);
                CHECK_EQ(spec.width, cursor.i64());
                CHECK_EQ(spec.stretch, cursor.i64());
                CHECK_EQ(spec.shrink, cursor.i64());
                CHECK(spec.stretch_order == GlueOrder::NORMAL);
                CHECK(spec.shrink_order == GlueOrder::NORMAL);
            } else {
                FAIL("a record this test does not know: %.*s", static_cast<int>(r.tag.size()),
                     r.tag.data());
                continue;
            }
            cursor.expect_done();
        }
    }
    CHECK(records > 150);
}

TEST(the_three_sizes_and_the_math_unit_match_the_reference)
{
    golden::File file;
    REQUIRE(file.load(golden::path("math/style.golden")));
    FontLibrary library;
    REQUIRE(math_golden::register_reference_fonts(library));
    math_golden::LibraryFonts real(library);
    math_golden::FakeFonts fake("fake", "none");
    core_golden::Context context;
    long records = 0;

    for (const core_golden::Case& c : core_golden::cases(file)) {
        if (c.name != "math-fonts")
            continue;
        core_golden::Reader reader(file, c);
        // One MathFonts per base size, rebuilt when a `math_fonts` record names
        // the next one.
        MathFonts fonts;
        Scaled current = 0;
        bool current_fake = false;
        while (!reader.done()) {
            const golden::Record& r = reader.take();
            CHECK_CONTEXT("style.golden line %d", r.line);
            golden::Cursor cursor(r);
            ++records;
            if (r.is("param_fields")) {
                const uint32_t count = cursor.u32();
                REQUIRE_EQ(count, uint32_t(IM_ARRAYSIZE(math_golden::PARAM_FIELDS)));
                for (const char* name : math_golden::PARAM_FIELDS)
                    CHECK_EQ(cursor.str(), std::string(name));
            } else if (r.is("math_fonts") || r.is("fake_math_fonts")) {
                current_fake = r.is("fake_math_fonts");
                current = cursor.i64();
                std::string why;
                math_golden::Fonts& source = current_fake ? static_cast<math_golden::Fonts&>(fake)
                                                          : static_cast<math_golden::Fonts&>(real);
                REQUIRE(fonts.init(current, source.math(), source.text(), &why));
                for (int i = 0; i < 3; ++i) {
                    const MathSize size = math_golden::math_size(cursor.raw());
                    CHECK_EQ(cursor.i64(), fonts.size(size));
                    CHECK_EQ(cursor.str(), context.faces.text(fonts.key(size)));
                }
            } else if (r.is("params")) {
                // The parameters of the face behind a key, to the last bit:
                // what the port's reader says against what fontTools said.
                const FontKey key = context.faces.key(cursor.str());
                const MathParams expected = math_golden::read_params_fields(cursor);
                bool found = false;
                for (const MathSize size :
                     {MathSize::TEXT, MathSize::SCRIPT, MathSize::SCRIPT_SCRIPT}) {
                    if (fonts.key(size) != key)
                        continue;
                    found = true;
                    const MathParams& ours = fonts.face(size).math();
                    const double* a = &expected.math_x_height;
                    const double* b = &ours.math_x_height;
                    for (size_t i = 0; i < IM_ARRAYSIZE(math_golden::PARAM_FIELDS); ++i) {
                        CHECK_CONTEXT("%s", math_golden::PARAM_FIELDS[i]);
                        CHECK_EQ(a[i], b[i]);
                    }
                    CHECK(math_golden::same_params(expected, ours));
                }
                CHECK(found);
            } else if (r.is("mu") || r.is("fake_mu")) {
                REQUIRE_EQ(cursor.i64(), current);
                REQUIRE_EQ(r.is("fake_mu"), current_fake);
                const Style s = math_golden::style(cursor.raw());
                CHECK_EQ(cursor.i64(), fonts.mu(s));
            } else {
                FAIL("a record this test does not know: %.*s", static_cast<int>(r.tag.size()),
                     r.tag.data());
                continue;
            }
            cursor.expect_done();
        }
    }
    CHECK(records > 100);
}

// --- The reference's tests, as properties --------------------------------------------

TEST(no_ordered_pair_of_atoms_reaches_an_impossible_table_entry)
{
    // The table has eleven '*' entries and the first pass's BIN conversion is
    // the only thing that keeps them unreachable.
    ExpectViolations watch;
    Bench bench;
    long pairs = 0;
    for (int left = 0; left <= static_cast<int>(AtomType::INNER); ++left) {
        for (int right = 0; right <= static_cast<int>(AtomType::INNER); ++right) {
            for (int s = 0; s < STYLE_COUNT; ++s) {
                std::vector<Node> out;
                const MathStatus status = bench.convert(
                    {of(static_cast<AtomType>(left), 'a'), of(static_cast<AtomType>(right), 'b')},
                    static_cast<Style>(s), out);
                CHECK(status.ok());
                ++pairs;
            }
        }
    }
    // And with a fraction, a boundary and a space among them.
    const std::vector<MathItem> mixed = {
        of(AtomType::BIN, '+'),
        MathItem::fraction(Field::math_char('x'), Field::math_char('y')),
        of(AtomType::BIN, '-'),
        MathItem::space(THIN_MU),
        MathItem::atom(AtomType::LEFT),
        of(AtomType::BIN, '+'),
        MathItem::atom(AtomType::RIGHT),
        of(AtomType::BIN, '*'),
    };
    std::vector<Node> out;
    CHECK(bench.convert(mixed, Style::TEXT, out).ok());
    CHECK_EQ(pairs, 8L * 8 * 8);
    CHECK_EQ(g_violations, 0);
}

TEST(a_space_is_not_something_for_a_binary_operator_to_bind)
{
    // A node, not a noad: it never becomes r, so the three demotions of the
    // first pass all see straight through it. One mu is 10 at this size.
    Bench bench;
    const Scaled MU = 10;
    CHECK_EQ(bench.fonts.mu(Style::TEXT), MU);
    const MathItem thin = MathItem::space(THIN_MU);
    const MathItem quad = MathItem::space(QUAD_MU);

    std::vector<Node> nodes = bench.set({thin, of(AtomType::BIN, '+'), ord_('b')}, Style::TEXT);
    CHECK(glue_widths(nodes) == std::vector<Scaled>{3 * MU});
    nodes = bench.set({ord_('a'), of(AtomType::BIN, '+'), quad}, Style::TEXT);
    CHECK(glue_widths(nodes) == std::vector<Scaled>{18 * MU});
    nodes = bench.set({ord_('a'), of(AtomType::BIN, '+'), thin, of(AtomType::REL, '='), ord_('b')},
                      Style::TEXT);
    CHECK(glue_widths(nodes) == (std::vector<Scaled>{3 * MU, 5 * MU, 5 * MU}));
    // The control: with something on both sides the same '+' is binary.
    nodes = bench.set({ord_('a'), thin, of(AtomType::BIN, '+'), thin, ord_('b')}, Style::TEXT);
    CHECK(glue_widths(nodes) == (std::vector<Scaled>{3 * MU, 4 * MU, 3 * MU, 4 * MU}));
}

TEST(a_space_in_a_script_is_measured_in_the_scripts_math_units)
{
    Bench bench;
    const Scaled script_mu = bench.fonts.mu(Style::SCRIPT);
    CHECK_EQ(script_mu, 7);
    const std::vector<MathItem> script = {ord_('i'), MathItem::space(THIN_MU), ord_('j')};
    const std::vector<Node> nodes =
        bench.set({ord_('x', Field(), bench.list.sub_list(script))}, Style::TEXT);
    const std::vector<Node> boxes = boxes_of(nodes);
    REQUIRE_EQ(boxes.size(), size_t(1));
    CHECK(glue_widths(bench.arena.contents(boxes[0])) == std::vector<Scaled>{3 * script_mu});
}

TEST(the_fraction_bar_is_centred_on_the_axis_and_as_thick_as_the_font_says)
{
    Bench bench;
    const Scaled axis = 180 / 4;
    const Scaled thickness = 180 / 20;
    const std::vector<Node> nodes =
        bench.set({MathItem::fraction(Field::math_char('x'), Field::math_char('y'))}, Style::TEXT);
    REQUIRE_EQ(nodes.size(), size_t(1));
    REQUIRE(nodes[0].is(NodeType::BOX));
    const Box& vlist = bench.arena.box(nodes[0]);
    CHECK(vlist.kind == BoxKind::VLIST);
    // Walk down from the top of the box to the rule.
    Scaled above_baseline = vlist.height;
    bool found = false;
    for (const Node& node : bench.arena.contents(nodes[0])) {
        if (node.is(NodeType::RULE)) {
            CHECK_EQ(node.height(), thickness);
            CHECK_EQ(node.depth(), 0);
            CHECK_EQ(node.width(), RUNNING);
            CHECK_EQ(above_baseline, axis + half(thickness));
            found = true;
            break;
        }
        if (node.is(NodeType::BOX)) {
            const Box& box = bench.arena.box(node);
            above_baseline -= box.height + box.depth;
        } else if (node.is(NodeType::KERN)) {
            above_baseline -= node.width();
        }
    }
    CHECK(found);

    // \atop: no rule at all, and two boxes.
    const std::vector<Node> atop = bench.set(
        {MathItem::fraction(Field::math_char('x'), Field::math_char('y'), 0)}, Style::TEXT);
    REQUIRE_EQ(atop.size(), size_t(1));
    long rules = 0;
    for (const Node& node : bench.arena.contents(atop[0]))
        rules += node.is(NodeType::RULE) ? 1 : 0;
    CHECK_EQ(rules, 0L);
    CHECK_EQ(boxes_of(bench.arena.contents(atop[0])).size(), size_t(2));
}

TEST(display_style_selects_a_different_summation_glyph_and_stacks_its_limits)
{
    // The central claim of the reference's litmus test: not the body glyph
    // scaled up but a different glyph, selected by following the font's own
    // variant chain exactly one step.
    FontLibrary library;
    REQUIRE(math_golden::register_reference_fonts(library));
    math_golden::LibraryFonts source(library);
    MathFonts fonts;
    REQUIRE(fonts.init(sp_from_px(32.0), source.math(), source.text()));
    const FaceData* math_face = library.face_data(Face::MATH);
    REQUIRE(math_face != nullptr);
    MathList list;
    NodeArena arena;
    const uint32_t SIGMA = 0x2211;

    const auto set = [&](const std::vector<MathItem>& items, Style style) {
        std::vector<Node> out;
        const MathStatus status = mlist_to_hlist(list, items, style, fonts, arena, out);
        CHECK(status.ok());
        return out;
    };
    const auto packed = [&](const std::vector<Node>& nodes) {
        return hpack(arena, nodes, 0, PackMode::ADDITIONAL, fonts.measure()).box;
    };

    const std::vector<MathItem> bare = {MathItem::atom(AtomType::OP, Field::math_char(SIGMA))};
    const std::vector<Node> display = set(bare, Style::DISPLAY);
    const std::vector<Node> text = set(bare, Style::TEXT);
    const std::vector<uint32_t> display_glyphs = codepoints(arena, display);
    const std::vector<uint32_t> text_glyphs = codepoints(arena, text);
    REQUIRE_EQ(display_glyphs.size(), size_t(1));
    REQUIRE_EQ(text_glyphs.size(), size_t(1));
    CHECK_EQ(std::string(math_face->glyph_name(display_glyphs[0])), std::string("summation.v1"));
    CHECK_EQ(std::string(math_face->glyph_name(text_glyphs[0])), std::string("summation"));
    const Box display_box = packed(display);
    const Box text_box = packed(text);
    CHECK(display_box.height + display_box.depth > text_box.height + text_box.depth);
    CHECK(fonts.key(Style::DISPLAY) == fonts.key(Style::TEXT));

    // Centred on the math axis and not on the baseline.
    const Scaled axis = round_to_scaled(fonts.params(Style::TEXT).axis_height);
    const Scaled middle = floor_div(text_box.depth - text_box.height, 2);
    CHECK(std::llabs(middle + axis) <= 1);

    // Stacked in display style: one vlist of the upper limit, the operator and
    // the lower one. Inline: the nucleus, then a box of scripts beside it.
    const std::vector<MathItem> lower = {ord_('i'), of(AtomType::REL, '='), ord_('1')};
    const std::vector<MathItem> bounded = {MathItem::atom(
        AtomType::OP, Field::math_char(SIGMA), Field::math_char('n'), list.sub_list(lower))};
    const std::vector<Node> stacked = boxes_of(set(bounded, Style::DISPLAY));
    REQUIRE_EQ(stacked.size(), size_t(1));
    CHECK(arena.box(stacked[0]).kind == BoxKind::VLIST);
    const std::vector<Node> parts = boxes_of(arena.contents(stacked[0]));
    REQUIRE_EQ(parts.size(), size_t(3));
    CHECK(codepoints(arena, arena.contents(parts[0])) == std::vector<uint32_t>{'n'});
    CHECK(codepoints(arena, arena.contents(parts[2])) == (std::vector<uint32_t>{'i', '=', '1'}));
    CHECK_EQ(boxes_of(set(bounded, Style::TEXT)).size(), size_t(2));

    // \nolimits keeps the bounds beside the operator even in display style.
    std::vector<MathItem> beside = bounded;
    beside[0].limits = Limits::NO_LIMITS;
    CHECK_EQ(boxes_of(set(beside, Style::DISPLAY)).size(), size_t(2));
}

TEST(a_borrowed_surd_brings_the_rule_thickness_of_the_font_it_came_from)
{
    // A radical in a script that outgrows the script chain takes a text-size
    // surd, whose stroke is the text font's 900 and not the script font's 630.
    Bench bench("chained", "none", 18000);
    bench.fonts.delimiter_shortfall = 9000;
    CHECK_EQ(bench.fonts.size(MathSize::SCRIPT), 12600);
    const Scaled script_rule =
        round_to_scaled(bench.fonts.params(Style::SCRIPT).radical_rule_thickness);
    const Scaled text_rule =
        round_to_scaled(bench.fonts.params(Style::TEXT).radical_rule_thickness);
    CHECK_EQ(script_rule, 630);
    CHECK_EQ(text_rule, 900);

    const auto radical_over = [&](Scaled height, FontKey* surd_font, Scaled* rule_height) {
        Box radicand;
        radicand.kind = BoxKind::HLIST;
        radicand.width = 1000;
        radicand.height = height;
        const Node box = bench.arena.add_box(radicand);
        const std::vector<Node> nodes =
            bench.set({MathItem::atom(AtomType::RADICAL, Field::sub_box(box))}, Style::SCRIPT);
        REQUIRE_EQ(nodes.size(), size_t(1));
        const NodeView parts = bench.arena.contents(nodes[0]);
        REQUIRE_EQ(parts.size(), size_t(2));
        const NodeView surd = bench.arena.contents(parts[0]);
        REQUIRE_EQ(surd.size(), size_t(1));
        *surd_font = surd[0].font();
        for (const Node& node : bench.arena.contents(parts[1])) {
            if (node.is(NodeType::RULE))
                *rule_height = node.height() + node.depth();
        }
        // The surd hangs so that the top of its ink is the top of the rule.
        const Box& surd_box = bench.arena.box(parts[0]);
        const Box& bar = bench.arena.box(parts[1]);
        CHECK_EQ(surd_box.height - surd_box.shift_amount, bar.height - *rule_height);
    };

    FontKey font;
    Scaled rule_height = 0;
    radical_over(1000, &font, &rule_height);
    CHECK(font == bench.fonts.key(MathSize::SCRIPT));
    CHECK_EQ(rule_height, script_rule);
    radical_over(68000, &font, &rule_height);
    CHECK(font == bench.fonts.key(MathSize::TEXT));
    CHECK_EQ(rule_height, text_rule);
}

// The next four state a boundary each as a property, on fakes of their own.
// Their expected values are the reference's, asked of it directly with the
// same fakes. The `boundary/` cases of mlist.golden stand on the same four
// with the reference's recorded answers; the test after these reads them.

TEST(the_largest_variant_must_cover_three_quarters_rounded_up)
{
    // LARGEST_VARIANT_MUST_COVER as a ceiling: with delta1 = 121 the largest
    // variant must reach ceil(2 * 121 * 3 / 4) = 182, and the bracket is 181.
    // Flooring would accept it. At delta1 = 120 it must reach 180 and does.
    Bench bench("chained", "none", 181);
    bench.fonts.delimiter_shortfall = 0;
    CHECK_EQ(round_to_scaled(bench.fonts.params(Style::TEXT).axis_height), 45);
    const auto bracketed = [&](Scaled height, std::vector<Node>& out) {
        Box content;
        content.width = 10;
        content.height = height;
        const Node box = bench.arena.add_box(content);
        return bench.convert({MathItem::atom(AtomType::LEFT, Field::math_char('[')),
                              MathItem::atom(AtomType::ORD, Field::sub_box(box)),
                              MathItem::atom(AtomType::RIGHT)},
                             Style::TEXT, out);
    };
    std::vector<Node> out;
    CHECK(bracketed(165, out).ok());
    REQUIRE_EQ(out.size(), size_t(3));
    REQUIRE(out[0].is(NodeType::BOX));
    const Box& bracket = bench.arena.box(out[0]);
    CHECK_EQ(bracket.height, 181);
    CHECK_EQ(bracket.shift_amount, 46);
    CHECK(codepoints(bench.arena, bench.arena.contents(out[0])) == std::vector<uint32_t>{'['});

    out.clear();
    const MathStatus refused = bracketed(166, out);
    CHECK(refused.error == MathError::DELIMITER_TOO_SMALL);
    CHECK_EQ(refused.codepoint, uint32_t('['));
    CHECK_EQ(refused.needed, 242);
    CHECK_EQ(refused.largest, 181);
}

TEST(a_tie_between_sizes_keeps_the_delimiter_found_first)
{
    // var_delimiter remembers a candidate only if it is strictly larger than
    // the largest so far, so of two fonts whose brackets are the same height
    // the one tried first, the smaller size, is the one kept.
    struct Fixed : math_golden::FakeMath {
        explicit Fixed(double size)
            : FakeMath(size, false)
        {
        }
        GlyphBBox bbox(uint32_t codepoint) const override
        {
            GlyphBBox box = FakeMath::bbox(codepoint);
            if (codepoint == '[')
                box.y_max = 100.0;
            return box;
        }
    };
    struct Source {
        std::map<Scaled, std::unique_ptr<Fixed>> faces;
        static const MathFontMetrics* resolve(void* user, Scaled size)
        {
            std::unique_ptr<Fixed>& slot = static_cast<Source*>(user)->faces[size];
            if (!slot)
                slot = std::make_unique<Fixed>(static_cast<double>(size));
            return slot.get();
        }
    } source;
    MathFonts fonts;
    REQUIRE(fonts.init(180, MathResolver{&Source::resolve, &source}));
    fonts.delimiter_shortfall = 0;
    MathList list;
    NodeArena arena;
    const auto bracketed = [&](Scaled height, Style style, std::vector<Node>& out) {
        Box content;
        content.width = 10;
        content.height = height;
        const Node box = arena.add_box(content);
        return mlist_to_hlist(
            list,
            std::vector<MathItem>{MathItem::atom(AtomType::LEFT, Field::math_char('[')),
                                  MathItem::atom(AtomType::ORD, Field::sub_box(box)),
                                  MathItem::atom(AtomType::RIGHT)},
            style, fonts, arena, out);
    };
    std::vector<Node> out;
    CHECK(bracketed(91, Style::SCRIPT, out).ok());
    REQUIRE_EQ(out.size(), size_t(3));
    REQUIRE(out[0].is(NodeType::BOX));
    const NodeView found = arena.contents(out[0]);
    REQUIRE_EQ(found.size(), size_t(1));
    CHECK(found[0].font() == fonts.key(MathSize::SCRIPT));
    CHECK_EQ(arena.box(out[0]).shift_amount, 19);

    out.clear();
    const MathStatus refused = bracketed(98, Style::SCRIPT, out);
    CHECK(refused.error == MathError::DELIMITER_TOO_SMALL);
    CHECK_EQ(refused.needed, 134);
    CHECK_EQ(refused.largest, 100);
    // In text style there is one font to try, and it is that one.
    out.clear();
    CHECK(bracketed(98, Style::TEXT, out).ok());
    REQUIRE_EQ(out.size(), size_t(3));
    CHECK(arena.contents(out[0])[0].font() == fonts.key(MathSize::TEXT));
    CHECK_EQ(arena.box(out[0]).shift_amount, 5);
}

TEST(a_big_operator_takes_exactly_one_step_along_its_chain)
{
    // make_op does not search: in display style it takes the successor of the
    // character and stops, however long the chain.
    Bench bench("chained", "none", 18000);
    const std::vector<MathItem> op = {
        MathItem::atom(AtomType::OP, Field::math_char('('), Field(), Field(), Limits::NO_LIMITS)};
    std::vector<Node> nodes = bench.set(op, Style::DISPLAY);
    REQUIRE_EQ(nodes.size(), size_t(1));
    CHECK(codepoints(bench.arena, nodes) == std::vector<uint32_t>{0xE001});
    CHECK_EQ(bench.arena.box(nodes[0]).height, 22500);
    CHECK_EQ(bench.arena.box(nodes[0]).depth, 13500);
    CHECK_EQ(bench.arena.box(nodes[0]).shift_amount, 0);
    nodes = bench.set(op, Style::TEXT);
    REQUIRE_EQ(nodes.size(), size_t(1));
    CHECK(codepoints(bench.arena, nodes) == std::vector<uint32_t>{'('});
    CHECK_EQ(bench.arena.box(nodes[0]).height, 13500);
    CHECK_EQ(bench.arena.box(nodes[0]).depth, 4500);
}

TEST(an_atop_in_display_style_clears_seven_rule_thicknesses)
{
    // Section 745: clr is 7 rule thicknesses in display style and 3 otherwise.
    // The rule is 9 here; a numerator 100 deep over a denominator 150 high is
    // short of both.
    Bench bench;
    const auto atop = [&](Style style, Scaled* height, Scaled* depth, Scaled* gap) {
        Box numerator;
        numerator.width = 10;
        numerator.height = 72;
        numerator.depth = 100;
        Box denominator;
        denominator.width = 10;
        denominator.height = 150;
        const Node above = bench.arena.add_box(numerator);
        const Node below = bench.arena.add_box(denominator);
        const std::vector<Node> nodes =
            bench.set({MathItem::fraction(Field::sub_box(above), Field::sub_box(below), 0)}, style);
        REQUIRE_EQ(nodes.size(), size_t(1));
        *height = bench.arena.box(nodes[0]).height;
        *depth = bench.arena.box(nodes[0]).depth;
        const NodeView contents = bench.arena.contents(nodes[0]);
        REQUIRE_EQ(contents.size(), size_t(3));
        REQUIRE(contents[1].is(NodeType::KERN));
        *gap = contents[1].width();
    };
    Scaled height = 0;
    Scaled depth = 0;
    Scaled gap = 0;
    atop(Style::DISPLAY, &height, &depth, &gap);
    CHECK_EQ(height, 229);
    CHECK_EQ(depth, 157);
    CHECK_EQ(gap, 64);
    atop(Style::TEXT, &height, &depth, &gap);
    CHECK_EQ(height, 220);
    CHECK_EQ(depth, 130);
    CHECK_EQ(gap, 28);
}

// --- mlist.golden: the `boundary/` cases ---------------------------------------------

namespace
{

constexpr Scaled NO_GAP = -1;

// What the reference answered for one `boundary/` case. For a list that set:
// the first glyph in reading order (0 if there is none), the height, depth and
// shift of the hlist's first box, and the kern that is the second thing in
// that box (NO_GAP if it is not a kern). For one that was refused: the
// delimiter, what was needed, and the largest found.
struct Boundary {
    const char* name;
    bool set;
    uint32_t glyph;
    Scaled height_or_needed;
    Scaled depth_or_largest;
    Scaled shift;
    Scaled gap;
};

constexpr Boundary BOUNDARIES[] = {
    // The largest bracket is 18001. Content reaching 12000 from the axis asks
    // it to cover 18000 and takes it; reaching 12001 asks for 18001.5, which
    // is 18002 rounded up and 18001 rounded down.
    {"boundary/largest-variant-cover/reached", true, '[', 18001, 0, 4501, NO_GAP},
    {"boundary/largest-variant-cover/one-short", false, '[', 21624, 18001, 0, NO_GAP},
    // Brackets 2 high at script size and at text size.
    {"boundary/delimiter-tie/script", false, '[', 719899, 2, 0, NO_GAP},
    // One step along a chain of four, and of eight; none outside display.
    {"boundary/op-one-step/chained/display", true, 0xE001, 22500, 13500, 0, NO_GAP},
    {"boundary/op-one-step/chained/display_cramped", true, 0xE001, 22500, 13500, 0, NO_GAP},
    {"boundary/op-one-step/chained/text", true, '(', 13500, 4500, 0, NO_GAP},
    {"boundary/op-one-step/chained/script", true, '(', 9450, 3150, 0, NO_GAP},
    {"boundary/op-one-step/real/display", true, 0xFFFBE, 1258273, 468892, 1, NO_GAP},
    {"boundary/op-one-step/real/text", true, '(', 1180914, 391533, 1, NO_GAP},
    // A rule of 9 (6 at script size): seven of them in display style, three otherwise.
    {"boundary/atop-clearance/display", true, 0, 229, 157, 0, 64},
    {"boundary/atop-clearance/display_cramped", true, 0, 229, 157, 0, 64},
    {"boundary/atop-clearance/text", true, 0, 220, 130, 0, 28},
    {"boundary/atop-clearance/script", true, 0, 212, 128, 0, 18},
};

// Whether a case records the question `tag` about `codepoint` on the face `key`.
bool asked(const golden::File& file, const core_golden::Case& c, std::string_view tag,
           std::string_view key, uint32_t codepoint)
{
    core_golden::Reader reader(file, c);
    while (!reader.done()) {
        const golden::Record& r = reader.take();
        if (r.is(tag) && r.size() >= 2 && r.str(0) == key && r.u32(1) == codepoint)
            return true;
    }
    return false;
}

} // namespace

TEST(the_boundary_cases_hold_the_references_answers_and_the_port_gives_them)
{
    golden::File file;
    REQUIRE(file.load(golden::path("math/mlist.golden")));
    const std::vector<core_golden::Case> cases = core_golden::cases(file);
    REQUIRE(!cases.empty());
    math_golden::ParamsTable params;
    FontLibrary library;
    REQUIRE(math_golden::register_reference_fonts(library));

    long found = 0;
    long in_file = 0;
    Tally tally;
    for (const core_golden::Case& c : cases) {
        if (c.kind == "table" && c.name == "font-params") {
            core_golden::Context context;
            params.read(file, c, context);
            continue;
        }
        if (c.name.rfind("boundary/", 0) != 0)
            continue;
        ++in_file;
        CHECK_CONTEXT("mlist.golden: case %s (line %d)", c.name.c_str(), c.line);
        const Boundary* expected = nullptr;
        for (const Boundary& row : BOUNDARIES) {
            if (c.name == row.name)
                expected = &row;
        }
        if (expected == nullptr) {
            FAIL("a boundary case with no row in the table");
            continue;
        }
        ++found;

        // The golden against the table.
        core_golden::Reader reader(file, c);
        core_golden::Context context;
        while (!reader.done() && !reader.next_is("nodes") && !reader.next_is("refused"))
            reader.take();
        if (!expected->set) {
            const golden::Record& refused = reader.take("refused");
            CHECK_EQ(std::string(refused.raw(0)), std::string("delimiter-too-small"));
            REQUIRE_EQ(refused.size(), size_t(4));
            CHECK_EQ(refused.u32(1), expected->glyph);
            CHECK_EQ(refused.i64(2), expected->height_or_needed);
            CHECK_EQ(refused.i64(3), expected->depth_or_largest);
        } else {
            REQUIRE(reader.next_is("nodes"));
            const std::vector<Node> nodes = core_golden::read_nodes(reader, context);
            REQUIRE(!nodes.empty());
            REQUIRE(nodes[0].is(NodeType::BOX));
            const Box& first = context.arena.box(nodes[0]);
            CHECK_EQ(first.height, expected->height_or_needed);
            CHECK_EQ(first.depth, expected->depth_or_largest);
            CHECK_EQ(first.shift_amount, expected->shift);
            const std::vector<uint32_t> glyphs = codepoints(context.arena, nodes);
            CHECK_EQ(glyphs.empty() ? uint32_t(0) : glyphs[0], expected->glyph);
            const NodeView contents = context.arena.contents(nodes[0]);
            const Scaled gap = contents.size() > 1 && contents[1].is(NodeType::KERN)
                                   ? contents[1].width()
                                   : NO_GAP;
            CHECK_EQ(gap, expected->gap);
        }

        // The port against the golden, under every kind of fonts that applies.
        for (const FontMode mode : {FontMode::RECORDED, FontMode::FAKE, FontMode::LIBRARY}) {
            CHECK_CONTEXT("%s fonts", mode_name(mode));
            run_mlist_case(file, c, params, mode, &library, tally);
        }
    }
    CHECK_EQ(found, static_cast<long>(sizeof(BOUNDARIES) / sizeof(BOUNDARIES[0])));
    CHECK_EQ(in_file, found);
    // Each case under recorded fonts, and under the one other kind it names.
    CHECK_EQ(tally.cases, 2 * found);

    // The tie is a refusal whichever bracket is kept, so what the case holds
    // is the questions: the reference measured the text-size bracket, passed
    // it over, and so never asked for the italic correction a box would need.
    // Replayed with recorded fonts, building that box is a question the
    // reference did not ask.
    for (const core_golden::Case& c : cases) {
        if (c.name != "boundary/delimiter-tie/script")
            continue;
        CHECK(asked(file, c, "q_bbox", "math@4", '['));
        CHECK(asked(file, c, "q_italic", "math@4", '['));
        CHECK(asked(file, c, "q_bbox", "math@5", '['));
        CHECK(!asked(file, c, "q_italic", "math@5", '['));
    }
}

TEST(an_accent_is_refused_and_is_not_a_layout_refusal)
{
    Bench bench;
    std::vector<Node> out;
    const MathStatus status =
        bench.convert({MathItem::atom(AtomType::ACCENT, Field::math_char('x'))}, Style::TEXT, out);
    CHECK(status.error == MathError::NOT_IMPLEMENTED);
    CHECK(!status.is_layout_refusal());
    CHECK(out.empty());
    CHECK(!math_error_message(status).empty());
}

// --- What is this port's own --------------------------------------------------------

TEST(a_failed_conversion_leaves_the_output_and_the_arena_as_they_were)
{
    Bench bench("chained", "none", 18000);
    Box tall;
    tall.height = 500000;
    tall.width = 10;
    const Node tall_box = bench.arena.add_box(tall);
    std::vector<Node> out = {kern(7)};
    const size_t boxes = bench.arena.box_count();
    const size_t nodes = bench.arena.node_count();
    // A fraction first, so boxes have been made by the time the radical fails.
    const MathStatus status =
        bench.convert({MathItem::fraction(Field::math_char('x'), Field::math_char('y')),
                       MathItem::atom(AtomType::RADICAL, Field::sub_box(tall_box))},
                      Style::TEXT, out);
    CHECK(status.error == MathError::DELIMITER_TOO_SMALL);
    CHECK_EQ(status.codepoint, SURD);
    CHECK(status.needed > status.largest);
    REQUIRE_EQ(out.size(), size_t(1));
    CHECK(out[0] == kern(7));
    CHECK_EQ(bench.arena.box_count(), boxes);
    CHECK_EQ(bench.arena.node_count(), nodes);
    CHECK_EQ(math_error_message(status),
             check::format("U+221A has no variant %lld sp tall (the largest is %lld sp), and "
                           "delimiters are not assembled from pieces here",
                           static_cast<long long>(status.needed),
                           static_cast<long long>(status.largest)));

    // And the same fonts set the next list.
    out.clear();
    CHECK(bench.convert({ord_('x')}, Style::TEXT, out).ok());
    CHECK_EQ(out.size(), size_t(1));
}

TEST(a_list_that_names_nothing_is_malformed_and_reads_nothing_out_of_bounds)
{
    Bench bench("fake", "fake");
    std::vector<Node> out;
    const auto refused = [&](const MathItem& item) {
        out.clear();
        const MathStatus status = bench.convert({ord_('a'), item}, Style::TEXT, out);
        CHECK(out.empty());
        return status.error;
    };

    // A sublist span past the end of the list, and one that wraps.
    Field field = Field::sub_list(MathSpan{5, 1000});
    CHECK(refused(MathItem::atom(AtomType::ORD, field)) == MathError::MALFORMED);
    field = Field::sub_list(MathSpan{0xFFFFFFFFu, 0xFFFFFFFFu});
    CHECK(refused(MathItem::atom(AtomType::ORD, Field(), field)) == MathError::MALFORMED);
    // Prose whose bytes are not there.
    field = bench.list.text("ab");
    field.span.count = 1u << 30;
    CHECK(refused(MathItem::atom(AtomType::ORD, field)) == MathError::MALFORMED);
    // A box index the arena does not hold, as a nucleus and as a script.
    field = Field::sub_box_index(123456);
    CHECK(refused(MathItem::atom(AtomType::ORD, field)) == MathError::MALFORMED);
    CHECK(refused(MathItem::atom(AtomType::ORD, Field::math_char('x'), field)) ==
          MathError::MALFORMED);
    CHECK(refused(MathItem::fraction(field, Field())) == MathError::MALFORMED);
    // A node that is not a box, offered as one.
    CHECK(refused(MathItem::atom(AtomType::ORD, Field::sub_box(kern(5)))) == MathError::MALFORMED);
    // Tags that are not members of their enums.
    field = Field();
    field.type = static_cast<FieldType>(200);
    CHECK(refused(MathItem::atom(AtomType::ORD, field)) == MathError::MALFORMED);
    CHECK(refused(MathItem::atom(AtomType::OP, Field::math_char('x'), field)) ==
          MathError::MALFORMED);
    MathItem odd = ord_('x');
    odd.kind = static_cast<AtomType>(99);
    CHECK(refused(odd) == MathError::MALFORMED);
    // A MU_GLUE where no space is, a SPACE without one, a boundary holding a list.
    CHECK(refused(MathItem::atom(AtomType::ORD, Field::mu_glue(THIN_MU))) == MathError::MALFORMED);
    CHECK(refused(MathItem::atom(AtomType::ORD, Field::math_char('x'), Field::mu_glue(THIN_MU))) ==
          MathError::MALFORMED);
    CHECK(refused(MathItem::atom(AtomType::SPACE, Field::math_char('x'))) == MathError::MALFORMED);
    CHECK(refused(MathItem::atom(AtomType::SPACE)) == MathError::MALFORMED);
    const std::vector<MathItem> inner = {ord_('(')};
    CHECK(refused(MathItem::atom(AtomType::LEFT, bench.list.sub_list(inner))) ==
          MathError::MALFORMED);
    // A style that is not one, a span of the top-level list that is not there,
    // and fonts that were never initialised.
    out.clear();
    CHECK(bench.convert({ord_('a')}, static_cast<Style>(8), out).error == MathError::MALFORMED);
    CHECK(mlist_to_hlist(bench.list, MathSpan{0, 1u << 20}, Style::TEXT, bench.fonts, bench.arena,
                         out)
              .error == MathError::MALFORMED);
    MathFonts unready;
    CHECK(mlist_to_hlist(bench.list, std::vector<MathItem>{ord_('a')}, Style::TEXT, unready,
                         bench.arena, out)
              .error == MathError::MALFORMED);
    CHECK(out.empty());
}

TEST(nesting_is_bounded_and_a_list_that_contains_itself_ends)
{
    Bench bench;
    // A tower of superscripts: x^{x^{x^...}}.
    const auto tower = [&](int levels) {
        Field field = Field::math_char('x');
        for (int i = 0; i < levels; ++i) {
            const std::vector<MathItem> one = {ord_('x', field)};
            field = bench.list.sub_list(one);
        }
        return std::vector<MathItem>{ord_('x', field)};
    };
    std::vector<Node> out;
    // The top-level list is level one, so this many sublists is the deepest
    // that sets.
    CHECK(bench.convert(tower(MAX_MATH_NESTING - 1), Style::TEXT, out).ok());
    CHECK_EQ(out.size(), size_t(2));
    out.clear();
    const MathStatus too_deep = bench.convert(tower(MAX_MATH_NESTING), Style::TEXT, out);
    CHECK(too_deep.error == MathError::TOO_DEEP);
    CHECK(!too_deep.is_layout_refusal());
    CHECK(out.empty());

    // A list whose nucleus names the list itself.
    MathList looped;
    std::vector<MathItem> self = {MathItem::atom(AtomType::ORD, Field::sub_list(MathSpan{0, 1}))};
    const MathSpan span = looped.add_items(self);
    CHECK_EQ(span.first, 0u);
    CHECK(mlist_to_hlist(looped, span, Style::TEXT, bench.fonts, bench.arena, out).error ==
          MathError::TOO_DEEP);
    CHECK(out.empty());

    // A box already as deep as the arena allows cannot be wrapped again.
    NodeArena deep;
    Box shell;
    shell.width = 10;
    shell.height = 10;
    Node inner = deep.add_box(shell);
    for (int i = 1; i < NodeArena::MAX_NESTING; ++i)
        inner = deep.add_box(shell, NodeView(&inner, 1));
    CHECK_EQ(deep.nesting(inner), NodeArena::MAX_NESTING);
    MathList plain;
    const MathStatus wrapped = mlist_to_hlist(
        plain, std::vector<MathItem>{MathItem::fraction(Field::sub_box(inner), Field())},
        Style::TEXT, bench.fonts, deep, out);
    CHECK(wrapped.error == MathError::TOO_DEEP);
    CHECK(out.empty());
}

TEST(a_list_that_shares_its_sublists_into_too_much_work_is_refused)
{
    // Forty levels, each naming the level below from two fields: forty items
    // in the list and 2^40 to translate.
    Bench bench;
    MathList shared;
    Field below = Field::math_char('x');
    for (int i = 0; i < 40; ++i) {
        const std::vector<MathItem> level = {ord_('x', below, below)};
        below = shared.sub_list(level);
    }
    CHECK_EQ(shared.item_count(), size_t(40));
    std::vector<Node> out;
    const size_t boxes = bench.arena.box_count();
    const MathStatus status = mlist_to_hlist(shared, std::vector<MathItem>{ord_('x', below, below)},
                                             Style::TEXT, bench.fonts, bench.arena, out);
    CHECK(status.error == MathError::TOO_LARGE);
    CHECK(!status.is_layout_refusal());
    CHECK(out.empty());
    CHECK_EQ(bench.arena.box_count(), boxes);
    // Sharing as such is not refused: ten levels is a thousand items.
    MathList modest;
    below = Field::math_char('x');
    for (int i = 0; i < 10; ++i) {
        const std::vector<MathItem> level = {ord_('x', below, below)};
        below = modest.sub_list(level);
    }
    CHECK(mlist_to_hlist(modest, std::vector<MathItem>{ord_('x', below, below)}, Style::TEXT,
                         bench.fonts, bench.arena, out)
              .ok());
    CHECK_EQ(out.size(), size_t(2));
}

TEST(prose_that_is_not_utf8_is_refused_as_the_replacement_character_it_decodes_to)
{
    Bench bench("fake", "fake");
    std::vector<Node> out;
    const MathStatus status =
        bench.convert({MathItem::atom(AtomType::ORD, bench.list.text("a\xFF"
                                                                     "b",
                                                                     Face::PROSE_BOLD))},
                      Style::TEXT, out);
    CHECK(status.error == MathError::MISSING_GLYPH);
    CHECK_EQ(status.codepoint, 0xFFFDu);
    CHECK(status.face == Face::PROSE_BOLD);
    CHECK_EQ(math_error_message(status),
             std::string("prose-bold has no glyph for '\xEF\xBF\xBD' (U+FFFD), and a formula is "
                         "not patched from another face"));
}

TEST(a_box_holding_another_fonts_characters_needs_a_text_face)
{
    // The reference raises TextFaceUnavailableError out of whatever asks its
    // measure about a key that is not a math key and has nothing to resolve
    // it. A box is packed by its own dimensions, so a SUB_BOX holding such a
    // character converts and packs; it is ship-out, which needs the character's
    // advance, that asks.
    Bench bench;
    const FontKey foreign = font_key(Face::CODE, 180);
    std::vector<Node> out = {char_node(foreign, 'x')};
    Box box;
    box.width = 90;
    box.height = 72;
    const Node sub = bench.arena.add_box(box, out);
    out.clear();
    const MathStatus status =
        bench.convert({MathItem::atom(AtomType::ORD, Field::sub_box(sub))}, Style::TEXT, out);
    CHECK(status.ok());
    CHECK(!bench.fonts.unmeasured());
    const PackResult packed =
        hpack(bench.arena, out, 0, PackMode::ADDITIONAL, bench.fonts.measure());
    const Node whole = bench.arena.add_box(packed.box, out);
    std::vector<DrawCmd> cmds;
    CHECK(ship(bench.arena, whole, Point{0, 0}, bench.fonts.measure(), cmds) == ShipError::NONE);
    CHECK(bench.fonts.unmeasured());
    bench.fonts.clear_unmeasured();
    CHECK(!bench.fonts.unmeasured());
    // A math key always resolves.
    const CharDimens known = bench.fonts.measure(bench.fonts.key(MathSize::SCRIPT), 'x');
    CHECK_EQ(known.width, 63);
    CHECK(!bench.fonts.unmeasured());
}

TEST(math_fonts_refuses_sizes_and_faces_it_cannot_use)
{
    math_golden::FakeFonts source("fake", "none");
    MathFonts fonts;
    std::string why;
    CHECK(!fonts.init(0, source.math(), source.text(), &why));
    CHECK(!why.empty());
    CHECK(!fonts.ready());
    CHECK(!fonts.init(-5, source.math()));
    CHECK(!fonts.init(MAX_DIMEN + 1, source.math()));
    CHECK(!fonts.init(180, MathResolver()));
    CHECK(fonts.init(MAX_DIMEN, source.math()));
    CHECK(fonts.ready());

    // A face whose script percentages are not usable.
    struct Odd : math_golden::FakeMath {
        Odd(double size, double scale)
            : FakeMath(size, false)
            , params(FakeMath::math())
        {
            params.script_scale = scale;
        }
        const MathParams& math() const override { return params; }
        MathParams params;
    };
    for (const double scale : {0.0, -0.7, std::nan(""), 1e30, HUGE_VAL}) {
        Odd odd(180.0, scale);
        const MathResolver one{
            [](void* user, Scaled) -> const MathFontMetrics* { return static_cast<Odd*>(user); },
            &odd};
        why.clear();
        CHECK(!fonts.init(180, one, MetricsResolver(), &why));
        CHECK(!why.empty());
        CHECK(!fonts.ready());
    }
    // A resolver with no face at a script size.
    math_golden::FakeMath only(180.0, false);
    const MathResolver at_180{[](void* user, Scaled size) -> const MathFontMetrics* {
                                  return size == 180 ? static_cast<math_golden::FakeMath*>(user)
                                                     : nullptr;
                              },
                              &only};
    CHECK(!fonts.init(180, at_180));
}

TEST(pythons_repr_of_a_character)
{
    CHECK_EQ(python_char_repr('a'), std::string("'a'"));
    CHECK_EQ(python_char_repr(' '), std::string("' '"));
    CHECK_EQ(python_char_repr('\''), std::string("\"'\""));
    CHECK_EQ(python_char_repr('"'), std::string("'\"'"));
    CHECK_EQ(python_char_repr('\\'), std::string("'\\\\'"));
    CHECK_EQ(python_char_repr('\t'), std::string("'\\t'"));
    CHECK_EQ(python_char_repr('\n'), std::string("'\\n'"));
    CHECK_EQ(python_char_repr('\r'), std::string("'\\r'"));
    CHECK_EQ(python_char_repr(0), std::string("'\\x00'"));
    CHECK_EQ(python_char_repr(0x7F), std::string("'\\x7f'"));
    CHECK_EQ(python_char_repr(0x9F), std::string("'\\x9f'"));
    CHECK_EQ(python_char_repr(0xA0), std::string("'\\xa0'"));
    CHECK_EQ(python_char_repr(0xAD), std::string("'\\xad'"));
    CHECK_EQ(python_char_repr(0xE9), std::string("'\xC3\xA9'"));
    CHECK_EQ(python_char_repr(0x200B), std::string("'\\u200b'"));
    CHECK_EQ(python_char_repr(0x20AC), std::string("'\xE2\x82\xAC'"));
    CHECK_EQ(python_char_repr(0xE000), std::string("'\\ue000'"));
    CHECK_EQ(python_char_repr(0x1D400), std::string("'\xF0\x9D\x90\x80'"));
    CHECK_EQ(python_char_repr(0x10FFFF), std::string("'\\U0010ffff'"));
}

TEST(a_math_list_copies_what_it_is_given_even_from_itself)
{
    MathList list;
    const std::vector<MathItem> three = {ord_('a'), ord_('b'), ord_('c')};
    const MathSpan first = list.add_items(three);
    CHECK_EQ(first.first, 0u);
    CHECK_EQ(first.count, 3u);
    MathView view;
    REQUIRE(list.view(first, &view));
    // Enough copies of its own storage to move it several times.
    MathSpan last = first;
    for (int i = 0; i < 12; ++i) {
        REQUIRE(list.view(last, &view));
        last = list.add_items(view);
    }
    REQUIRE(list.view(last, &view));
    REQUIRE_EQ(view.size, size_t(3));
    CHECK_EQ(view[0].nucleus.codepoint, uint32_t('a'));
    CHECK_EQ(view[2].nucleus.codepoint, uint32_t('c'));
    CHECK_EQ(list.item_count(), size_t(39));
    CHECK(!list.view(MathSpan{38, 2}, &view));
    CHECK(view.empty());
    REQUIRE(list.view(MathSpan{39, 0}, &view));

    const Field prose = list.text("if ", Face::PROSE_ITALIC);
    std::string_view bytes;
    REQUIRE(list.text_of(prose.span, &bytes));
    CHECK_EQ(std::string(bytes), std::string("if "));
    CHECK(prose.face == Face::PROSE_ITALIC);
    CHECK(!list.text_of(MathSpan{2, 2}, &bytes));
    list.clear();
    CHECK_EQ(list.item_count(), size_t(0));
    CHECK(!list.text_of(prose.span, &bytes));
}

TEST(math_glue_stays_inside_the_dimension_invariant)
{
    // The reference's integers are unbounded; a host's are whatever it wrote.
    const Scaled huge = Scaled(1) << 60;
    const GlueSpec spec = from_mu(MuGlue{huge, -huge, 3}, 58254);
    CHECK_EQ(spec.width, MAX_DIMEN);
    CHECK_EQ(spec.stretch, -MAX_DIMEN);
    CHECK_EQ(spec.shrink, 3 * 58254);
    Bench bench;
    std::vector<Node> out;
    CHECK(bench
              .convert({ord_('a'), MathItem::space(MuGlue{huge, huge, huge}), ord_('b')},
                       Style::TEXT, out)
              .ok());
    REQUIRE_EQ(out.size(), size_t(3));
    CHECK_EQ(out[1].width(), MAX_DIMEN);
    // A fraction rule as thick as a host says is bounded the same way, and
    // the fraction around a rule that thick is then a box taller than a box
    // may be: refused as too large, with the dimension, and nothing kept.
    out.clear();
    const size_t boxes = bench.arena.box_count();
    const MathStatus thick = bench.convert(
        {MathItem::fraction(Field::math_char('x'), Field::math_char('y'), huge)}, Style::TEXT, out);
    CHECK(thick.error == MathError::TOO_LARGE);
    CHECK(thick.needed > MAX_DIMEN);
    CHECK_EQ(thick.largest, MAX_DIMEN);
    CHECK(out.empty());
    CHECK_EQ(bench.arena.box_count(), boxes);
    // One that is thick and fits is set, its rule as thick as was said.
    const Scaled thickness = MAX_DIMEN / 4;
    REQUIRE(
        bench
            .convert({MathItem::fraction(Field::math_char('x'), Field::math_char('y'), thickness)},
                     Style::TEXT, out)
            .ok());
    REQUIRE_EQ(out.size(), size_t(1));
    bool ruled = false;
    for (const Node& node : bench.arena.contents(out[0])) {
        if (node.is(NodeType::RULE)) {
            CHECK_EQ(node.height(), thickness);
            ruled = true;
        }
    }
    CHECK(ruled);
}

// --- A font whose parameters cancel -----------------------------------------------------

namespace
{

// A math face whose big-operator spacings are out of all proportion and
// cancel: a padding of a million ems outside each limit and a distance of
// minus a million ems between the limit and the operator. Nothing bounds a number in a MATH table.
// The stack of limits that section 750 builds states its own height and depth, in which the two
// cancel, so the box is an ordinary size and holds two kerns no node may hold (src/units.h).
class CancellingSpacings : public math_golden::FakeMath
{
public:
    explicit CancellingSpacings(double size)
        : math_golden::FakeMath(size, false)
        , _params(math_golden::FakeMath::math())
    {
        _params.big_op_spacing5 = 1.0e6 * size;
        _params.big_op_spacing1 = -1.0e6 * size;
        _params.big_op_spacing2 = -1.0e6 * size;
        _params.big_op_spacing3 = -1.0e6 * size;
        _params.big_op_spacing4 = -1.0e6 * size;
    }
    const MathParams& math() const override { return _params; }

private:
    MathParams _params;
};

const MathFontMetrics* cancelling_spacings(void*, Scaled size)
{
    static std::vector<std::unique_ptr<CancellingSpacings>> made;
    made.push_back(std::make_unique<CancellingSpacings>(static_cast<double>(size)));
    return made.back().get();
}

// Whether every dimension stored anywhere under a node is inside +-MAX_DIMEN.
bool held_to_max_dimen(const NodeArena& arena, const Node& node, int depth = 0)
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
            if (!held_to_max_dimen(arena, child, depth + 1))
                return false;
        }
        return true;
    }
    if (node.type == NodeType::KERN || node.type == NodeType::GLUE || node.type == NodeType::RULE)
        return inside(node.a) && inside(node.b) && inside(node.c);
    return true;
}

} // namespace

TEST(two_kerns_that_cancel_inside_an_ordinary_box_are_refused_and_not_stored)
{
    MathFonts fonts;
    REQUIRE(fonts.init(18000, MathResolver{&cancelling_spacings, nullptr}, MetricsResolver()));
    MathList list;
    NodeArena arena;
    MathItem sum = MathItem::atom(AtomType::OP, Field::math_char(0x2211), Field::math_char('n'),
                                  Field::math_char('i'));
    const std::vector<MathItem> items = {sum};
    for (const TexRules rules : {TexRules::reference(), TexRules::product()}) {
        fonts.rules = rules;
        std::vector<Node> out;
        const size_t boxes = arena.box_count();
        const MathStatus status = mlist_to_hlist(list, items, Style::DISPLAY, fonts, arena, out);
        // Refused, with the kern that was a million ems, and nothing kept.
        CHECK(status.error == MathError::TOO_LARGE);
        CHECK(status.inside);
        CHECK(status.needed > MAX_DIMEN || status.needed < -MAX_DIMEN);
        CHECK_EQ(status.largest, MAX_DIMEN);
        CHECK(out.empty());
        CHECK_EQ(arena.box_count(), boxes);
        CHECK_EQ(math_error_message(status),
                 std::string("a space or a rule inside it is larger than the largest dimension "
                             "there is"));
    }
    // The same operator with the limits beside it stacks nothing and is set,
    // every dimension in it inside the bound.
    fonts.rules = TexRules::reference();
    std::vector<Node> out;
    REQUIRE(mlist_to_hlist(list, items, Style::TEXT, fonts, arena, out).ok());
    REQUIRE(!out.empty());
    for (const Node& node : out)
        CHECK(held_to_max_dimen(arena, node));
}

// A fraction whose rule is a million ems thick the wrong way: the kern above
// it and the rule cancel inside the height section 747 states.
TEST(a_rule_that_cancels_against_its_kerns_is_refused_and_not_stored)
{
    class NegativeRule : public math_golden::FakeMath
    {
    public:
        explicit NegativeRule(double size)
            : math_golden::FakeMath(size, false)
            , _params(math_golden::FakeMath::math())
        {
            _params.default_rule_thickness = -1.0e6 * size;
        }
        const MathParams& math() const override { return _params; }

    private:
        MathParams _params;
    };
    static std::vector<std::unique_ptr<NegativeRule>> made;
    const MathResolver resolver{[](void*, Scaled size) -> const MathFontMetrics* {
                                    made.push_back(
                                        std::make_unique<NegativeRule>(static_cast<double>(size)));
                                    return made.back().get();
                                },
                                nullptr};
    MathFonts fonts;
    REQUIRE(fonts.init(18000, resolver, MetricsResolver()));
    MathList list;
    NodeArena arena;
    const std::vector<MathItem> items = {
        MathItem::fraction(Field::math_char('x'), Field::math_char('y'))};
    for (const Style style : {Style::TEXT, Style::DISPLAY}) {
        std::vector<Node> out;
        const MathStatus status = mlist_to_hlist(list, items, style, fonts, arena, out);
        if (status.ok()) {
            for (const Node& node : out)
                CHECK(held_to_max_dimen(arena, node));
        } else {
            CHECK(status.error == MathError::TOO_LARGE);
            CHECK(out.empty());
            CHECK_EQ(arena.box_count(), size_t(0));
        }
    }
}
