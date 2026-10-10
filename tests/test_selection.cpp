// src/selection against the reference: every case of tests/golden/selection
// (the lines found, where points land, what ranges select, the rectangles and
// the text of a range). Then against the reference's rules restated the slow
// way, over random command buffers, since the port finds lines by bisection
// and the reference by scanning; and what is this port's own: no allocation in
// a point query, the Unicode tables, and behaviour at coordinates no document
// has.

#include "core_golden.h"

#include "selection.h"
#include "shipout.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

// --- Counting allocations ---------------------------------------------------------
// Replaced for the whole test binary, so that "a point query allocates nothing"
// is observed and not assumed.

namespace
{
long g_allocations = 0;
}

void* operator new(std::size_t size)
{
    ++g_allocations;
    void* p = std::malloc(size != 0 ? size : 1);
    if (p == nullptr)
        std::abort();
    return p;
}
void* operator new[](std::size_t size)
{
    return operator new(size);
}
// The nothrow forms too: the standard library's temporary buffers (stable_sort)
// are allocated with them and released with the plain delete.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    ++g_allocations;
    return std::malloc(size != 0 ? size : 1);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    ++g_allocations;
    return std::malloc(size != 0 ? size : 1);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

namespace
{

std::vector<DrawCmd> to_port(const std::vector<core_golden::Cmd>& cmds)
{
    std::vector<DrawCmd> out;
    out.reserve(cmds.size());
    for (const core_golden::Cmd& c : cmds) {
        switch (c.kind) {
        case core_golden::Cmd::GLYPH:
            out.push_back(
                DrawCmd::glyph(c.font, c.codepoint, c.x, c.y, static_cast<uint32_t>(c.index)));
            break;
        case core_golden::Cmd::RULERECT:
            out.push_back(DrawCmd::rule_rect(c.x, c.y, c.width, c.height));
            break;
        case core_golden::Cmd::SETGLUE:
            out.push_back(DrawCmd::set_glue(c.x, c.y, c.width, c.natural, c.height, c.depth,
                                            static_cast<uint32_t>(c.index)));
            break;
        case core_golden::Cmd::ATOM:
            out.push_back(DrawCmd::atom(c.x, c.y, c.width, c.natural, c.height, c.depth,
                                        static_cast<uint32_t>(c.index)));
            break;
        case core_golden::Cmd::LINE:
            out.push_back(DrawCmd::line(c.x, c.y, c.width, c.height, c.depth,
                                        static_cast<uint32_t>(c.start),
                                        static_cast<uint32_t>(c.end)));
            break;
        }
    }
    return out;
}

Granularity granularity(std::string_view token)
{
    if (token == "character")
        return Granularity::CHARACTER;
    if (token == "word")
        return Granularity::WORD;
    if (token != "line")
        FAIL("not a granularity: %s", std::string(token).c_str());
    return Granularity::LINE;
}

// One `textdata` entry of a case: the four mappings, owning their strings.
struct Data {
    std::vector<uint32_t> hard_breaks;
    std::vector<uint32_t> keys[3];
    std::vector<std::string> values[3];
    std::vector<IndexedText> entries[3];

    void read_mapping(core_golden::Reader& reader, const char* tag, int which)
    {
        const golden::Record& record = reader.take(tag);
        golden::Cursor cursor(record);
        const uint32_t count = cursor.u32();
        for (uint32_t i = 0; i < count; ++i) {
            keys[which].push_back(cursor.u32());
            values[which].push_back(cursor.str());
        }
        cursor.expect_done();
    }

    // Views are made last, when the strings have stopped moving.
    TextData view()
    {
        for (int which = 0; which < 3; ++which) {
            entries[which].clear();
            for (size_t i = 0; i < keys[which].size(); ++i)
                entries[which].push_back(
                    IndexedText{keys[which][i], values[which][i].data(), values[which][i].size()});
        }
        TextData data;
        data.hard_breaks = hard_breaks.data();
        data.hard_break_count = hard_breaks.size();
        data.separators = entries[0].data();
        data.separator_count = entries[0].size();
        data.atoms = entries[1].data();
        data.atom_count = entries[1].size();
        data.originals = entries[2].data();
        data.original_count = entries[2].size();
        return data;
    }
};

struct Tally {
    long cases = 0;
    long fragments = 0;
    long probes = 0;
    long selects = 0;
    long drags = 0;
    long ranges = 0;
    long rects = 0;
    long texts = 0;
    long raised = 0;    // ranges for which the reference's rects() raised
    long clustered = 0; // cases in which every fragment is a cluster of its own
};

void replay(const golden::File& file, const core_golden::Case& c, Tally& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);
    const std::vector<DrawCmd> cmds = to_port(core_golden::read_cmds(reader, context));
    SelectionModel model;
    model.build(cmds);

    {
        const golden::Record& record = reader.take("model");
        CHECK_EQ(model.length(), record.u32(0));
        REQUIRE_EQ(model.fragment_count(), record.u32(1));
    }
    for (size_t ordinal = 0; ordinal < model.fragment_count(); ++ordinal) {
        CHECK_CONTEXT("fragment %zu", ordinal);
        const Fragment& f = model.fragment(ordinal);
        const golden::Record& record = reader.take("fragment");
        CHECK_EQ(f.line, record.u32(0));
        CHECK_EQ(f.start, record.u32(1));
        CHECK_EQ(f.end, record.u32(2));
        CHECK_EQ(f.outer, record.i64(3));
        CHECK_EQ(f.top, record.i64(4));
        CHECK_EQ(f.bottom, record.i64(5));
        CHECK_EQ(f.reach_top, record.i64(6));
        CHECK_EQ(f.reach_bottom, record.i64(7));
        const golden::Record& indices = reader.take("indices");
        REQUIRE_EQ(f.item_count, indices.u32(0));
        for (uint32_t k = 0; k < f.item_count; ++k)
            CHECK_EQ(model.indices(f)[k], indices.u32(1 + k));
        const golden::Record& edges = reader.take("edges");
        REQUIRE_EQ(f.item_count + 1, edges.u32(0));
        for (uint32_t k = 0; k <= f.item_count; ++k)
            CHECK_EQ(model.edges(f)[k], edges.i64(1 + k));
        ++tally.fragments;
    }

    const uint32_t probes = reader.take("probes").u32(0);
    for (uint32_t i = 0; i < probes; ++i) {
        const golden::Record& record = reader.take("probe");
        const Point point{record.i64(0), record.i64(1)};
        CHECK_CONTEXT("probe (%lld, %lld), line %d", static_cast<long long>(point.x),
                      static_cast<long long>(point.y), record.line);
        CHECK_EQ(model.fragment_at(point), record.i64(2));
        CHECK_EQ(model.hit(point), record.u32(3));
        const Selection word = model.word_at(point);
        CHECK_EQ(word.start, record.u32(4));
        CHECK_EQ(word.end, record.u32(5));
        const Selection line = model.line_at(point);
        CHECK_EQ(line.start, record.u32(6));
        CHECK_EQ(line.end, record.u32(7));
        ++tally.probes;
    }

    const uint32_t selects = reader.take("selects").u32(0);
    for (uint32_t i = 0; i < selects; ++i) {
        const golden::Record& record = reader.take("select");
        const Selection selection = model.select(record.i64(0), record.i64(1));
        CHECK_EQ(selection.start, record.u32(2));
        CHECK_EQ(selection.end, record.u32(3));
        ++tally.selects;
    }

    const uint32_t drags = reader.take("drags").u32(0);
    for (uint32_t i = 0; i < drags; ++i) {
        const golden::Record& record = reader.take("drag");
        CHECK_CONTEXT("drag at line %d", record.line);
        const Selection selection =
            model.select_at(Point{record.i64(0), record.i64(1)},
                            Point{record.i64(2), record.i64(3)}, granularity(record.raw(4)));
        CHECK_EQ(selection.start, record.u32(5));
        CHECK_EQ(selection.end, record.u32(6));
        ++tally.drags;
    }

    const uint32_t ranges = reader.take("ranges").u32(0);
    std::vector<Rect> rects;
    for (uint32_t i = 0; i < ranges; ++i) {
        const golden::Record& record = reader.take("range");
        CHECK_CONTEXT("range [%u, %u)", record.u32(0), record.u32(1));
        rects.clear();
        model.rects(Selection{record.u32(0), record.u32(1)}, rects);
        if (record.i64(2) < 0) {
            // The reference raised: a line whose indices do not ascend. The
            // port has answered without one, which is all that is asked.
            ++tally.raised;
            continue;
        }
        const uint32_t expected = record.u32(2);
        CHECK_EQ(rects.size(), expected);
        for (uint32_t k = 0; k < expected; ++k) {
            const golden::Record& rect = reader.take("rect");
            if (k < rects.size()) {
                CHECK_EQ(rects[k].x, rect.i64(0));
                CHECK_EQ(rects[k].y, rect.i64(1));
                CHECK_EQ(rects[k].width, rect.i64(2));
                CHECK_EQ(rects[k].height, rect.i64(3));
            }
            ++tally.rects;
        }
        ++tally.ranges;
    }

    const uint32_t data_count = reader.take("textdata").u32(0);
    std::vector<Data> data(data_count);
    for (Data& entry : data) {
        const golden::Record& record = reader.take("hard_breaks");
        golden::Cursor cursor(record);
        const uint32_t count = cursor.u32();
        for (uint32_t i = 0; i < count; ++i)
            entry.hard_breaks.push_back(cursor.u32());
        cursor.expect_done();
        entry.read_mapping(reader, "separators", 0);
        entry.read_mapping(reader, "atoms", 1);
        entry.read_mapping(reader, "originals", 2);
    }
    const uint32_t texts = reader.take("texts").u32(0);
    std::string text;
    for (uint32_t i = 0; i < texts; ++i) {
        const golden::Record& record = reader.take("text");
        CHECK_CONTEXT("text [%u, %u) with data %u, line %d", record.u32(0), record.u32(1),
                      record.u32(2), record.line);
        REQUIRE(record.u32(2) < data.size());
        text.clear();
        model.text(Selection{record.u32(0), record.u32(1)}, data[record.u32(2)].view(), text);
        CHECK_EQ(text, record.str(3));
        ++tally.texts;
    }
    CHECK(reader.done());
    if (model.cluster_count() == model.fragment_count())
        ++tally.clustered;
    ++tally.cases;
}

} // namespace

TEST(selection_matches_the_reference)
{
    const std::vector<std::string> files = golden::list("selection");
    REQUIRE_EQ(files.size(), 2u);
    Tally tally;
    for (const std::string& path : files) {
        CHECK_CONTEXT("%s", path.c_str());
        golden::File file;
        if (!CHECK(file.load(path)))
            continue;
        for (const core_golden::Case& c : core_golden::cases(file)) {
            CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
            REQUIRE_EQ(c.kind, "selection");
            replay(file, c, tally);
        }
    }
    CHECK(tally.cases >= 54);
    CHECK(tally.fragments > 240);
    CHECK(tally.probes > 11000);
    CHECK(tally.selects > 800);
    CHECK(tally.drags > 1200);
    CHECK(tally.ranges > 1000);
    CHECK(tally.rects > 1000);
    CHECK(tally.texts > 5000);
    CHECK(tally.raised > 0);
    // Both ways of finding a line are in there: cases where every line is a
    // cluster of its own, and cases where lines share one.
    CHECK(tally.clustered > 10);
    CHECK(tally.cases - tally.clustered > 10);
}

// --- The rules, restated the slow way ---------------------------------------------
// The reference's fragment_nearest, _boundary_in, rects and text, written over
// the model's public data with nothing clever: every fragment scanned, every
// edge walked, pieces in a list. The goldens tie the port to the reference;
// this ties the bisecting port to the scanning rule on buffers no golden has.

namespace
{

Scaled beside_slow(const Fragment& f, Scaled x)
{
    return std::max(std::max(f.x - x, x - (f.x + f.width)), Scaled(0));
}

int fragment_at_slow(const SelectionModel& model, Point point)
{
    int under = -1;
    int inside = -1;
    int nearest = -1;
    Scaled under_by = 0;
    Scaled inside_by = 0;
    Scaled nearest_far = 0;
    Scaled nearest_aside = 0;
    for (size_t i = 0; i < model.fragment_count(); ++i) {
        const Fragment& f = model.fragment(i);
        const Scaled aside = beside_slow(f, point.x);
        const bool contains = f.top <= point.y && point.y < f.bottom;
        if (aside == 0 && f.reach_top <= point.y && point.y < f.reach_bottom) {
            const Scaled distance =
                contains ? 0 : std::max(std::max(f.top - point.y, point.y - f.bottom), Scaled(0));
            if (under < 0 || distance < under_by) {
                under = static_cast<int>(i);
                under_by = distance;
            }
        }
        if (contains) {
            if (inside < 0 || aside < inside_by) {
                inside = static_cast<int>(i);
                inside_by = aside;
            }
        } else if (inside < 0 && under < 0) {
            const Scaled far = std::max(f.top - point.y, point.y - f.bottom);
            if (nearest < 0 || far < nearest_far || (far == nearest_far && aside < nearest_aside)) {
                nearest = static_cast<int>(i);
                nearest_far = far;
                nearest_aside = aside;
            }
        }
    }
    if (under >= 0)
        return under;
    return inside >= 0 ? inside : nearest;
}

Boundary hit_slow(const SelectionModel& model, Point point)
{
    const int ordinal = fragment_at_slow(model, point);
    if (ordinal < 0)
        return 0;
    const Fragment& f = model.fragment(static_cast<size_t>(ordinal));
    if (f.item_count == 0)
        return f.start;
    const Scaled* e = model.edges(f);
    const Boundary* index = model.indices(f);
    if (point.x < e[0])
        return index[0];
    for (uint32_t k = 0; k < f.item_count; ++k) {
        if (point.x < e[k + 1])
            return index[k] + (2 * point.x < e[k] + e[k + 1] ? 0u : 1u);
    }
    return index[f.item_count - 1] + 1;
}

// The offsets of a fragment's items with indices in [first, last], found by
// looking at every one. Only meaningful where the indices ascend, which the
// buffers this is used on guarantee.
bool owned_slow(const SelectionModel& model, const Fragment& f, int64_t first, int64_t last,
                uint32_t& low, uint32_t& high)
{
    bool any = false;
    for (uint32_t k = 0; k < f.item_count; ++k) {
        const int64_t index = model.indices(f)[k];
        if (index < first || index > last)
            continue;
        if (!any)
            low = k;
        high = k;
        any = true;
    }
    return any;
}

std::vector<Rect> rects_slow(const SelectionModel& model, Selection selection)
{
    std::vector<Rect> out;
    if (selection.is_empty())
        return out;
    for (size_t i = 0; i < model.fragment_count(); ++i) {
        const Fragment& f = model.fragment(i);
        uint32_t low = 0;
        uint32_t high = 0;
        if (!owned_slow(model, f, std::max(selection.start, f.start),
                        static_cast<int64_t>(std::min(selection.end, f.end)) - 1, low, high))
            continue;
        int64_t a = low;
        int64_t b = high;
        while (a <= b && model.kinds(f)[a] == ItemKind::FILL)
            ++a;
        while (b >= a && model.kinds(f)[b] == ItemKind::FILL)
            --b;
        if (a > b)
            continue;
        const Scaled* e = model.edges(f);
        out.push_back(Rect{e[a], f.top, e[b + 1] - e[a], f.bottom - f.top});
    }
    return out;
}

void separate_slow(std::vector<std::string>& pieces, const std::string& separator)
{
    if (separator == " ") {
        const std::string& last = pieces.back();
        if (last.empty() || (last.back() != ' ' && last.back() != '\n' && last.back() != '\t'))
            pieces.push_back(" ");
        return;
    }
    while (!pieces.empty() && pieces.back().find_first_not_of(' ') == std::string::npos)
        pieces.pop_back();
    if (pieces.empty())
        return;
    std::string& last = pieces.back();
    last.erase(last.find_last_not_of(' ') + 1);
    pieces.push_back(separator);
}

std::string utf8(uint32_t c)
{
    std::string out;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

const IndexedText* find_slow(const IndexedText* entries, size_t count, uint32_t index)
{
    for (size_t i = 0; i < count; ++i)
        if (entries[i].index == index)
            return &entries[i];
    return nullptr;
}

std::string text_slow(const SelectionModel& model, Selection selection, const TextData& data)
{
    if (selection.is_empty())
        return std::string();
    std::vector<std::string> pieces;
    size_t written = 0;
    for (size_t i = 0; i < model.fragment_count(); ++i) {
        const Fragment& f = model.fragment(i);
        if (f.item_count == 0) {
            if (!pieces.empty() && selection.start < f.start && f.start < selection.end)
                separate_slow(pieces, "\n");
            continue;
        }
        uint32_t low = 0;
        uint32_t high = 0;
        if (!owned_slow(model, f, std::max(selection.start, f.start),
                        static_cast<int64_t>(std::min(selection.end, f.end)) - 1, low, high))
            continue;
        bool opening = true;
        bool filled = false;
        for (uint32_t k = low; k <= high; ++k) {
            const uint32_t index = model.indices(f)[k];
            const ItemKind kind = model.kinds(f)[k];
            bool separated = false;
            std::string separator;
            if (const IndexedText* given =
                    find_slow(data.separators, data.separator_count, index)) {
                separated = true;
                separator.assign(given->text, given->size);
            }
            if (!separated && index == f.start &&
                std::find(data.hard_breaks, data.hard_breaks + data.hard_break_count, index) !=
                    data.hard_breaks + data.hard_break_count) {
                separated = true;
                separator = "\n";
            }
            if (!separated && opening && !pieces.empty()) {
                separated = true;
                separator = " ";
            }
            opening = false;
            if (separated) {
                if (!pieces.empty())
                    separate_slow(pieces, separator);
                filled = false;
            }
            if (kind == ItemKind::FILL) {
                filled = !separated;
                continue;
            }
            if (filled) {
                if (!pieces.empty())
                    separate_slow(pieces, " ");
                filled = false;
            }
            std::string rendered;
            if (kind == ItemKind::ATOM) {
                if (const IndexedText* named = find_slow(data.atoms, data.atom_count, index))
                    rendered.assign(named->text, named->size);
            } else if (kind == ItemKind::SPACE) {
                rendered = " ";
            } else if (const IndexedText* original =
                           find_slow(data.originals, data.original_count, index)) {
                rendered.assign(original->text, original->size);
            } else {
                rendered = utf8(model.codepoints(f)[k]);
            }
            if (rendered.empty())
                continue;
            pieces.push_back(rendered);
            written = pieces.size();
        }
    }
    std::string out;
    for (size_t i = 0; i < pieces.size() && i < written; ++i)
        out += pieces[i];
    return out;
}

// A small deterministic generator (the LCG the oracle scripts use).
struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed)
        : state(seed)
    {
    }
    uint32_t bits()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(state >> 33);
    }
    int below(int n) { return static_cast<int>(bits() % static_cast<uint32_t>(n)); }
    int between(int low, int high) { return low + below(high - low + 1); }
    bool chance(int numerator, int denominator) { return below(denominator) < numerator; }
};

// A buffer shaped like ship-out's -- every LINE before what it covers, indices
// dense and ascending, atoms followed by their ink -- with geometry that is
// anything at all: bands that overlap, touch, repeat or are empty, lines beside
// each other, nested three deep, rules and blank lines among them.
struct Shaper {
    Rng rng;
    std::vector<DrawCmd> cmds;
    uint32_t index = 0;
    int mode;

    Shaper(uint64_t seed, int m)
        : rng(seed)
        , mode(m)
    {
    }

    Scaled line_y(int row)
    {
        switch (mode) {
        case 0: // a column: evenly spaced bands that touch or leave a gap
            return 10 + row * 20;
        case 1: // a column with bands that overlap their neighbours
            return 10 + row * 7;
        case 2: // anywhere
            return rng.between(-40, 160);
        default: // few distinct heights: many lines level with each other
            return 10 + rng.below(4) * 20;
        }
    }

    void line(int depth, int row)
    {
        const size_t slot = cmds.size();
        const Scaled x = mode == 0 && depth == 0 ? 0 : rng.between(-30, 120);
        const Scaled y = line_y(row);
        const Scaled height = rng.chance(1, 12) ? 0 : rng.between(1, 12);
        const Scaled drop = rng.chance(1, 12) ? 0 : rng.between(0, 8);
        cmds.push_back(
            DrawCmd::line(x, y, 0, height, mode == 0 ? 20 - height : drop, index, index));
        Scaled pen = x;
        const int things = rng.chance(1, 8) ? 0 : rng.between(1, 9);
        for (int i = 0; i < things; ++i) {
            const int what = rng.below(20);
            const Scaled advance = rng.chance(1, 10) ? 0 : rng.between(1, 14);
            if (what < 9) {
                const uint32_t codepoints[] = {'a', 'Z', '7',  '_',    '-',
                                               '.', ' ', 0xE9, 0x4E2D, 0x3000};
                cmds.push_back(DrawCmd::glyph(FontKey::make(0, 10), codepoints[rng.below(10)], pen,
                                              y, index++));
            } else if (what < 12) {
                cmds.push_back(DrawCmd::set_glue(pen, y, advance, advance, height, drop, index++));
            } else if (what < 15) {
                cmds.push_back(DrawCmd::set_glue(pen, y, advance, 0, height, drop, index++));
            } else if (what < 17) {
                cmds.push_back(DrawCmd::atom(pen, y, advance, advance, height, drop, index));
                for (int ink = rng.below(3); ink > 0; --ink)
                    cmds.push_back(DrawCmd::glyph(FontKey::make(6, 10), 'x', pen, y, index));
                ++index;
            } else if (what < 18) {
                cmds.push_back(DrawCmd::rule_rect(pen, y - 1, advance, 1));
            } else if (depth < 2) {
                line(depth + 1, row + rng.below(3));
            }
            // Mostly rightward; sometimes back, so edges are not sorted.
            pen += rng.chance(1, 15) ? -advance : advance;
        }
        cmds[slot].width = rng.chance(1, 10) ? rng.between(0, 200) : pen - x;
        cmds[slot].end = index;
    }
};

TextData no_data()
{
    return TextData();
}

} // namespace

TEST(bisection_answers_as_the_scan_does_on_random_buffers)
{
    long buffers = 0;
    long points = 0;
    long ranges = 0;
    long clustered = 0;
    long scanned = 0;
    std::vector<Rect> rects;
    std::string text;
    SelectionModel model; // one model, rebuilt: storage reuse is part of what is tested
    for (int mode = 0; mode < 4; ++mode) {
        for (uint64_t seed = 1; seed <= 150; ++seed) {
            CHECK_CONTEXT("mode %d seed %llu", mode, static_cast<unsigned long long>(seed));
            Shaper shaper(seed * 7919 + static_cast<uint64_t>(mode), mode);
            const int lines = shaper.rng.between(0, 14);
            for (int row = 0; row < lines; ++row)
                shaper.line(0, row);
            model.build(shaper.cmds);
            ++buffers;
            if (model.cluster_count() == model.fragment_count())
                ++clustered;
            else
                ++scanned;

            // A separator at some line starts and inside some lines, a hard
            // break at others, atoms named and not.
            std::vector<uint32_t> breaks;
            std::vector<IndexedText> separators;
            std::vector<IndexedText> atoms;
            const char* const choices[] = {"\n", "\n\n", "\t", " ", "", "--"};
            for (uint32_t b = 0; b <= model.length(); ++b) {
                if (shaper.rng.chance(1, 9))
                    breaks.push_back(b);
                if (shaper.rng.chance(1, 11)) {
                    const char* chosen = choices[shaper.rng.below(6)];
                    separators.push_back(
                        IndexedText{b, chosen, std::char_traits<char>::length(chosen)});
                }
                if (shaper.rng.chance(1, 2))
                    atoms.push_back(IndexedText{b, "$f$", 3});
            }
            TextData data;
            data.hard_breaks = breaks.data();
            data.hard_break_count = breaks.size();
            data.separators = separators.data();
            data.separator_count = separators.size();
            data.atoms = atoms.data();
            data.atom_count = atoms.size();

            long wrong = 0;
            for (int i = 0; i < 400; ++i) {
                const Point point{shaper.rng.between(-80, 280), shaper.rng.between(-70, 330)};
                const int expected = fragment_at_slow(model, point);
                const int got = model.fragment_at(point);
                const Boundary expected_hit = hit_slow(model, point);
                if (got != expected || model.hit(point) != expected_hit) {
                    if (wrong++ < 3)
                        FAIL("at (%lld, %lld): fragment %d, the scan says %d; hit %u, the scan "
                             "says %u",
                             static_cast<long long>(point.x), static_cast<long long>(point.y), got,
                             expected, model.hit(point), expected_hit);
                }
                ++points;
            }
            CHECK_EQ(wrong, 0);

            for (int i = 0; i < 40; ++i) {
                const Selection selection =
                    model.select(shaper.rng.between(-2, static_cast<int>(model.length()) + 2),
                                 shaper.rng.between(-2, static_cast<int>(model.length()) + 2));
                rects.clear();
                model.rects(selection, rects);
                const std::vector<Rect> expected = rects_slow(model, selection);
                if (!CHECK(rects == expected))
                    FAIL("rects of [%u, %u): %zu, the scan says %zu", selection.start,
                         selection.end, rects.size(), expected.size());
                for (const TextData& with : {no_data(), data}) {
                    text.clear();
                    model.text(selection, with, text);
                    CHECK_EQ(text, text_slow(model, selection, with));
                }
                ++ranges;
            }
        }
    }
    CHECK_EQ(buffers, 600);
    CHECK(points == 240000 && ranges == 24000);
    // The generator reaches both: buffers where bisection alone finds the line
    // and buffers where a cluster has to be scanned.
    CHECK(clustered > 60);
    CHECK(scanned > 200);
}

// --- This port's own -----------------------------------------------------------------

namespace
{

// `lines` lines of `per_line` characters, one cell of 10 each, 20 apart.
std::vector<DrawCmd> column(int lines, int per_line)
{
    std::vector<DrawCmd> cmds;
    uint32_t index = 0;
    for (int row = 0; row < lines; ++row) {
        const Scaled y = 8 + Scaled(row) * 20;
        cmds.push_back(DrawCmd::line(0, y, Scaled(per_line) * 10, 8, 12, index,
                                     index + static_cast<uint32_t>(per_line)));
        for (int k = 0; k < per_line; ++k) {
            if (k % 6 == 5)
                cmds.push_back(DrawCmd::set_glue(Scaled(k) * 10, y, 10, 10, 8, 12, index++));
            else
                cmds.push_back(DrawCmd::glyph(FontKey::make(0, 10),
                                              static_cast<uint32_t>('a' + k % 26), Scaled(k) * 10,
                                              y, index++));
        }
    }
    return cmds;
}

} // namespace

TEST(point_queries_allocate_nothing)
{
    const std::vector<DrawCmd> cmds = column(200, 60);
    SelectionModel model;
    model.build(cmds);
    std::vector<Rect> rects;
    rects.reserve(256);

    Rng rng(42);
    unsigned long long checksum = 0;
    const long before = g_allocations;
    for (int i = 0; i < 20000; ++i) {
        const Point a{rng.between(-50, 700), rng.between(-50, 4100)};
        const Point b{rng.between(-50, 700), rng.between(-50, 4100)};
        checksum += static_cast<unsigned>(model.fragment_at(a) + 1);
        checksum += model.hit(a);
        checksum += model.word_at(a).end;
        checksum += model.line_at(b).start;
        checksum += model.select_at(a, b, static_cast<Granularity>(i % 3)).end;
        checksum += model.select(i - 5, 9000 - i).start;
    }
    const long during_points = g_allocations - before;
    // A range's rectangles go into storage the caller already has.
    rects.clear();
    model.rects(Selection{100, 5000}, rects);
    const long during_rects = g_allocations - before - during_points;
    CHECK_EQ(during_points, 0);
    CHECK_EQ(during_rects, 0);
    CHECK(checksum != 0);
    CHECK_EQ(rects.size(), 83u);

    // Rebuilding over the same buffer reuses what the model holds.
    const long before_rebuild = g_allocations;
    model.build(cmds);
    CHECK(g_allocations - before_rebuild <= 1); // the cluster pass's scratch
}

// Lines down a column are each a cluster of their own, so a point is found by
// bisection. The arithmetic says which line and which boundary; a hundred
// thousand lines are answered in the time a handful take.
TEST(a_long_column_is_bisected_not_scanned)
{
    const int lines = 100000;
    const int per_line = 12;
    const std::vector<DrawCmd> cmds = column(lines, per_line);
    SelectionModel model;
    model.build(cmds);
    REQUIRE_EQ(model.fragment_count(), static_cast<size_t>(lines));
    CHECK_EQ(model.cluster_count(), static_cast<size_t>(lines));
    CHECK_EQ(model.length(), static_cast<uint32_t>(lines * per_line));

    Rng rng(7);
    long wrong = 0;
    const int queries = 200000;
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < queries; ++i) {
        const int row = rng.below(lines);
        const int cell = rng.below(per_line);
        const int within = rng.below(10);
        const Point point{Scaled(cell) * 10 + within, Scaled(row) * 20 + rng.below(20)};
        const uint32_t expected =
            static_cast<uint32_t>(row * per_line + cell + (within < 5 ? 0 : 1));
        wrong += model.hit(point) != expected;
        wrong += model.fragment_at(point) != row;
    }
    const auto end = std::chrono::steady_clock::now();
    CHECK_EQ(wrong, 0);
    const double ns = std::chrono::duration<double, std::nano>(end - begin).count() / queries;
    std::printf("selection: %.0f ns per hit + fragment_at over %d lines (not asserted)\n", ns,
                lines);

    // Above everything and below everything: the first line and the last.
    CHECK_EQ(model.fragment_at(Point{50, -1000000}), 0);
    CHECK_EQ(model.fragment_at(Point{50, 1000000000}), lines - 1);
    CHECK_EQ(model.hit(Point{-5, Scaled(lines - 1) * 20 + 3}),
             static_cast<uint32_t>((lines - 1) * per_line));
    CHECK_EQ(model.hit(Point{100000, 3}), static_cast<uint32_t>(per_line));

    // A range in the middle touches three lines and looks at no others: its
    // rectangles are those three.
    std::vector<Rect> rects;
    const uint32_t start = 50000u * 12u + 3u;
    model.rects(Selection{start, start + 24u}, rects);
    REQUIRE_EQ(rects.size(), 3u);
    CHECK(rects[0] == (Rect{30, 50000 * 20, 90, 20}));
    CHECK(rects[1] == (Rect{0, 50001 * 20, 120, 20}));
    CHECK(rects[2] == (Rect{0, 50002 * 20, 30, 20}));
    std::string text;
    model.text(Selection{start, start + 24u}, text);
    // Each line is "abcde ghijk " (a space in every sixth cell); the wrap adds
    // no second space after the one a line ends with.
    CHECK_EQ(text, "de ghijk abcde ghijk abc");
}

TEST(an_empty_model_answers_every_question)
{
    SelectionModel model;
    std::vector<Rect> rects;
    std::string text;
    for (int pass = 0; pass < 2; ++pass) {
        CHECK_EQ(model.length(), 0u);
        CHECK_EQ(model.fragment_count(), 0u);
        CHECK_EQ(model.fragment_at(Point{3, 4}), -1);
        CHECK_EQ(model.hit(Point{3, 4}), 0u);
        CHECK(model.word_at(Point{3, 4}) == (Selection{0, 0}));
        CHECK(model.line_at(Point{3, 4}) == (Selection{0, 0}));
        CHECK(model.select(5, -5) == (Selection{0, 0}));
        model.rects(Selection{0, 10}, rects);
        model.text(Selection{0, 10}, text);
        CHECK(rects.empty());
        CHECK(text.empty());
        model.build(nullptr, 0); // and the same after building from nothing
    }
}

TEST(a_model_can_be_rebuilt_from_a_different_buffer)
{
    SelectionModel model;
    model.build(column(30, 20));
    CHECK_EQ(model.length(), 600u);
    const std::vector<DrawCmd> small = column(2, 3);
    model.build(small);
    CHECK_EQ(model.length(), 6u);
    CHECK_EQ(model.fragment_count(), 2u);
    CHECK_EQ(model.cluster_count(), 2u);
    CHECK_EQ(model.hit(Point{14, 30}), 4u);
    std::string text;
    model.text(Selection{0, 6}, text);
    CHECK_EQ(text, "abc abc");
}

// Points no document has. Nothing here may overflow: this test is for the
// sanitiser build as much as for its checks.
TEST(points_at_the_ends_of_the_number_line_are_clamped_not_overflowed)
{
    SelectionModel model;
    model.build(column(3, 4));
    const Scaled most = std::numeric_limits<Scaled>::max();
    const Scaled least = std::numeric_limits<Scaled>::min();
    CHECK_EQ(model.fragment_at(Point{most, most}), 2);
    CHECK_EQ(model.fragment_at(Point{least, least}), 0);
    CHECK_EQ(model.hit(Point{most, least}), 4u);
    CHECK_EQ(model.hit(Point{least, most}), 8u);
    CHECK(model.word_at(Point{most, 30}) == (Selection{4, 8}));
    CHECK(model.select_at(Point{least, least}, Point{most, most}, Granularity::CHARACTER) ==
          (Selection{0, 12}));
    CHECK(model.select(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()) ==
          (Selection{0, 12}));
}

TEST(text_is_utf8_and_a_codepoint_that_is_no_character_is_replaced)
{
    const FontKey font = FontKey::make(0, 10);
    const uint32_t codepoints[] = {'a', 0xE9, 0x4E2D, 0x1D465, 0xD800, 0x110000, 0x7F, 0};
    std::vector<DrawCmd> cmds;
    cmds.push_back(DrawCmd::line(0, 8, 80, 8, 2, 0, 8));
    for (uint32_t i = 0; i < 8; ++i)
        cmds.push_back(DrawCmd::glyph(font, codepoints[i], Scaled(i) * 10, 8, i));
    SelectionModel model;
    model.build(cmds);
    std::string text = "kept:";
    model.text(Selection{0, 8}, text);
    // Appended to what was there. A lone surrogate and a value past U+10FFFF
    // are not characters and are written as U+FFFD; DEL and NUL are.
    const std::string expected =
        std::string("kept:a\xC3\xA9\xE4\xB8\xAD\xF0\x9D\x91\xA5\xEF\xBF\xBD\xEF\xBF\xBD\x7F") +
        std::string(1, '\0');
    CHECK_EQ(text.size(), expected.size());
    CHECK(text == expected);
}

TEST(selection_is_a_half_open_range)
{
    const Selection none{4, 4};
    const Selection some{2, 5};
    const Selection backwards{5, 2};
    CHECK(none.is_empty());
    CHECK(!some.is_empty());
    CHECK(backwards.is_empty());
    CHECK(!some.covers(1));
    CHECK(some.covers(2));
    CHECK(some.covers(4));
    CHECK(!some.covers(5));
    CHECK(some == (Selection{2, 5}));
    CHECK(some != none);
}

// The tables src/selection.cpp carries for str.isalnum and str.isspace, against
// the ranges the math oracle dumped from the same interpreter.
TEST(the_unicode_tables_are_pythons)
{
    golden::File file;
    REQUIRE(file.load(golden::path("math/unicode.golden")));
    std::vector<uint8_t> alnum(0x110000, 0);
    std::vector<uint8_t> space(0x110000, 0);
    std::vector<uint8_t>* into = nullptr;
    long ranges = 0;
    for (const golden::Record& record : file) {
        if (record.is("predicate")) {
            const std::string name(record.raw(0));
            into = name == "isalnum" ? &alnum : (name == "isspace" ? &space : nullptr);
        } else if (record.is("range") && into != nullptr) {
            for (uint32_t c = record.u32(0); c <= record.u32(1) && c < 0x110000; ++c)
                (*into)[c] = 1;
            ++ranges;
        }
    }
    CHECK(ranges > 700);
    long wrong = 0;
    for (uint32_t c = 0; c < 0x110000; ++c) {
        if (is_alnum(c) != (alnum[c] != 0) || is_space(c) != (space[c] != 0)) {
            if (wrong++ < 5)
                FAIL("U+%04X: is_alnum %d, is_space %d", c, is_alnum(c), is_space(c));
        }
    }
    CHECK_EQ(wrong, 0);
    CHECK(!is_alnum(0x110000) && !is_space(0xFFFFFFFFu));
    CHECK(is_alnum('_') == false); // the underscore is a word character by rule, not by table
}
