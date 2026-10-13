// Fonts written from scratch, for the tests that state exactly the structure
// they are about: tests/test_stress_font.cpp (hostile fonts, taken all the way
// to a baked glyph) and tests/test_opentype_formats.cpp (the formats no
// bundled font has, with expectations worked out by hand from the OpenType and
// CFF specifications).
//
// Everything is inline and in namespace font_builders; a test says
// `using namespace font_builders;`.

#pragma once

#include "golden.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/resource.h>

namespace font_builders
{

using Bytes = std::vector<uint8_t>;

// --- Bytes ---------------------------------------------------------------------------

constexpr uint32_t tag(const char (&t)[5])
{
    return (uint32_t(static_cast<uint8_t>(t[0])) << 24) |
           (uint32_t(static_cast<uint8_t>(t[1])) << 16) |
           (uint32_t(static_cast<uint8_t>(t[2])) << 8) | uint32_t(static_cast<uint8_t>(t[3]));
}

inline uint32_t rd16(const Bytes& b, size_t at)
{
    return at + 2 <= b.size() ? (uint32_t(b[at]) << 8) | b[at + 1] : 0u;
}

inline uint32_t rd32(const Bytes& b, size_t at)
{
    return at + 4 <= b.size() ? (rd16(b, at) << 16) | rd16(b, at + 2) : 0u;
}

// Writes that fall outside the buffer are dropped: a mutation aimed past the
// end of a table is simply not made.
inline void wr16(Bytes& b, size_t at, uint32_t v)
{
    if (at + 2 > b.size())
        return;
    b[at] = static_cast<uint8_t>(v >> 8);
    b[at + 1] = static_cast<uint8_t>(v);
}

inline void wr32(Bytes& b, size_t at, uint32_t v)
{
    wr16(b, at, v >> 16);
    wr16(b, at + 2, v & 0xFFFFu);
}

inline void put8(Bytes& b, uint32_t v)
{
    b.push_back(static_cast<uint8_t>(v));
}

inline void put16(Bytes& b, uint32_t v)
{
    put8(b, v >> 8);
    put8(b, v);
}

inline void put32(Bytes& b, uint32_t v)
{
    put16(b, v >> 16);
    put16(b, v & 0xFFFFu);
}

inline void append(Bytes& b, const Bytes& more)
{
    b.insert(b.end(), more.begin(), more.end());
}

// splitmix64: the same sequence on every platform.
struct Rng {
    uint64_t state;

    uint64_t next()
    {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // Uniform enough for a fuzzer; 0 when n is 0.
    size_t below(size_t n) { return n == 0 ? 0 : static_cast<size_t>(next() % n); }
    bool chance(size_t one_in) { return below(one_in) == 0; }
};

// --- An sfnt taken apart and put back -------------------------------------------------

struct SfntTable {
    uint32_t tag = 0;
    Bytes data;
};

struct Sfnt {
    uint32_t version = 0x00010000u;
    std::vector<SfntTable> tables;

    SfntTable* find(uint32_t wanted)
    {
        for (SfntTable& t : tables)
            if (t.tag == wanted)
                return &t;
        return nullptr;
    }

    Bytes& table(uint32_t wanted)
    {
        if (SfntTable* t = find(wanted))
            return t->data;
        tables.push_back({wanted, {}});
        return tables.back().data;
    }

    void remove(uint32_t wanted)
    {
        tables.erase(std::remove_if(tables.begin(), tables.end(),
                                    [&](const SfntTable& t) { return t.tag == wanted; }),
                     tables.end());
    }

    // Records in tag order, each table on a four-byte boundary. Checksums are
    // left zero: nothing downstream verifies them.
    Bytes build() const
    {
        std::vector<const SfntTable*> order;
        for (const SfntTable& t : tables)
            order.push_back(&t);
        std::stable_sort(order.begin(), order.end(),
                         [](const SfntTable* a, const SfntTable* b) { return a->tag < b->tag; });
        Bytes out;
        put32(out, version);
        put16(out, static_cast<uint32_t>(order.size()));
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        size_t at = 12 + order.size() * 16;
        for (const SfntTable* t : order) {
            put32(out, t->tag);
            put32(out, 0);
            put32(out, static_cast<uint32_t>(at));
            put32(out, static_cast<uint32_t>(t->data.size()));
            at += (t->data.size() + 3) & ~size_t(3);
        }
        for (const SfntTable* t : order) {
            append(out, t->data);
            while (out.size() % 4 != 0)
                out.push_back(0);
        }
        return out;
    }
};

inline bool parse_sfnt(const Bytes& file, Sfnt& out)
{
    out = Sfnt();
    if (file.size() < 12)
        return false;
    out.version = rd32(file, 0);
    const size_t count = rd16(file, 4);
    for (size_t i = 0; i < count; ++i) {
        const size_t record = 12 + i * 16;
        const size_t offset = rd32(file, record + 8), length = rd32(file, record + 12);
        if (record + 16 > file.size() || offset > file.size() || length > file.size() - offset)
            return false;
        out.tables.push_back({rd32(file, record),
                              Bytes(file.begin() + static_cast<std::ptrdiff_t>(offset),
                                    file.begin() + static_cast<std::ptrdiff_t>(offset + length))});
    }
    return true;
}

// Where a table's record and data sit in a file, for mutations made in place.
struct TableSpan {
    uint32_t tag = 0;
    size_t record = 0, offset = 0, length = 0;
};

// Where each face's table directory starts: 0 for a single font, the offsets
// a collection's header lists for a 'ttcf' file (the first 64, and only those
// inside the file).
inline std::vector<size_t> face_directories(const Bytes& file)
{
    if (rd32(file, 0) != tag("ttcf"))
        return {0};
    std::vector<size_t> out;
    const size_t count = std::min<size_t>(rd32(file, 8), 64);
    for (size_t i = 0; i < count && 12 + (i + 1) * 4 <= file.size(); ++i) {
        const size_t at = rd32(file, 12 + i * 4);
        if (at < file.size() && std::find(out.begin(), out.end(), at) == out.end())
            out.push_back(at);
    }
    return out;
}

// The tables of every face of the file, face by face.
inline std::vector<TableSpan> table_spans(const Bytes& file)
{
    std::vector<TableSpan> out;
    for (const size_t directory : face_directories(file)) {
        const size_t count = rd16(file, directory + 4);
        for (size_t i = 0; i < count && directory + 12 + (i + 1) * 16 <= file.size(); ++i) {
            const size_t record = directory + 12 + i * 16;
            TableSpan span;
            span.tag = rd32(file, record);
            span.record = record;
            span.offset = rd32(file, record + 8);
            span.length = rd32(file, record + 12);
            if (span.offset <= file.size() && span.length <= file.size() - span.offset)
                out.push_back(span);
        }
    }
    return out;
}

inline const TableSpan* span_of(const std::vector<TableSpan>& spans, uint32_t wanted)
{
    for (const TableSpan& s : spans)
        if (s.tag == wanted)
            return &s;
    return nullptr;
}

// --- Tables written from scratch ------------------------------------------------------

struct CmapGroup {
    uint32_t first, last, glyph;
};

inline Bytes cmap_sub12or13(uint32_t format, const std::vector<CmapGroup>& groups);
inline Bytes cmap_sub4(const std::vector<CmapGroup>& segments);

// A cmap of one (3,10) format 12 subtable.
inline Bytes cmap_format12(const std::vector<CmapGroup>& groups)
{
    Bytes out;
    put16(out, 0);
    put16(out, 1);
    put16(out, 3);
    put16(out, 10);
    put32(out, 12);
    append(out, cmap_sub12or13(12, groups));
    return out;
}

// A cmap of one (3,1) format 4 subtable.
inline Bytes cmap_format4(const std::vector<CmapGroup>& segments)
{
    Bytes out;
    put16(out, 0);
    put16(out, 1);
    put16(out, 3);
    put16(out, 1);
    put32(out, 12);
    append(out, cmap_sub4(segments));
    return out;
}

// A simple glyph: one closed contour through `points`, all on the curve.
inline Bytes simple_glyph(const std::vector<std::array<int, 2>>& points)
{
    int x_min = points[0][0], x_max = x_min, y_min = points[0][1], y_max = y_min;
    for (const auto& p : points) {
        x_min = std::min(x_min, p[0]);
        x_max = std::max(x_max, p[0]);
        y_min = std::min(y_min, p[1]);
        y_max = std::max(y_max, p[1]);
    }
    Bytes out;
    put16(out, 1);
    put16(out, static_cast<uint32_t>(x_min));
    put16(out, static_cast<uint32_t>(y_min));
    put16(out, static_cast<uint32_t>(x_max));
    put16(out, static_cast<uint32_t>(y_max));
    put16(out, static_cast<uint32_t>(points.size() - 1));
    put16(out, 0);
    for (size_t i = 0; i < points.size(); ++i)
        put8(out, 0x01);
    int previous = 0;
    for (const auto& p : points) {
        put16(out, static_cast<uint32_t>(p[0] - previous));
        previous = p[0];
    }
    previous = 0;
    for (const auto& p : points) {
        put16(out, static_cast<uint32_t>(p[1] - previous));
        previous = p[1];
    }
    return out;
}

inline Bytes square_glyph(int x, int y, int side)
{
    return simple_glyph({{{x, y}}, {{x, y + side}}, {{x + side, y + side}}, {{x + side, y}}});
}

struct Component {
    uint32_t glyph = 0;
    int dx = 0, dy = 0;
};

// A composite glyph of components placed by offset, with no transform.
inline Bytes composite_glyph(const std::vector<Component>& components)
{
    Bytes out;
    put16(out, 0xFFFF);
    put16(out, 0);
    put16(out, 0);
    put16(out, 500);
    put16(out, 500);
    for (size_t i = 0; i < components.size(); ++i) {
        const bool more = i + 1 < components.size();
        put16(out, 0x0001u | 0x0002u | (more ? 0x0020u : 0u)); // words, x/y values
        put16(out, components[i].glyph);
        put16(out, static_cast<uint32_t>(components[i].dx));
        put16(out, static_cast<uint32_t>(components[i].dy));
    }
    return out;
}

// A whole small font, every table written here, so a case can state exactly
// the structure it is about.
struct MiniFont {
    int units_per_em = 1000;
    int ascender = 800, descender = -200;
    bool long_loca = true;
    std::vector<Bytes> glyphs; // glyf data per glyph; empty for no outline
    Bytes cff;                 // used in place of glyf when not empty
    uint32_t glyph_count = 0;  // for CFF; glyf fonts count `glyphs`
    std::vector<CmapGroup> cmap;
    int advance = 600;

    Sfnt sfnt() const
    {
        const bool is_cff = !cff.empty();
        const uint32_t count = is_cff ? glyph_count : static_cast<uint32_t>(glyphs.size());
        Sfnt f;
        f.tables.reserve(16); // the references below must survive the later tables
        f.version = is_cff ? tag("OTTO") : 0x00010000u;

        Bytes& head = f.table(tag("head"));
        head.assign(54, 0);
        wr32(head, 0, 0x00010000u);
        wr32(head, 12, 0x5F0F3CF5u);
        wr16(head, 18, static_cast<uint32_t>(units_per_em));
        wr16(head, 36, 0);
        wr16(head, 38, static_cast<uint32_t>(descender));
        wr16(head, 40, 1000);
        wr16(head, 42, static_cast<uint32_t>(ascender));
        wr16(head, 46, 8); // lowestRecPPEM
        wr16(head, 50, long_loca ? 1 : 0);

        Bytes& hhea = f.table(tag("hhea"));
        hhea.assign(36, 0);
        wr32(hhea, 0, 0x00010000u);
        wr16(hhea, 4, static_cast<uint32_t>(ascender));
        wr16(hhea, 6, static_cast<uint32_t>(descender));
        wr16(hhea, 10, static_cast<uint32_t>(advance));
        wr16(hhea, 34, count);

        Bytes& maxp = f.table(tag("maxp"));
        if (is_cff) {
            put32(maxp, 0x00005000u);
            put16(maxp, count);
        } else {
            maxp.assign(32, 0);
            wr32(maxp, 0, 0x00010000u);
            wr16(maxp, 4, count);
            wr16(maxp, 6, 64);  // maxPoints
            wr16(maxp, 8, 8);   // maxContours
            wr16(maxp, 10, 64); // maxCompositePoints
            wr16(maxp, 12, 8);  // maxCompositeContours
            wr16(maxp, 14, 1);  // maxZones
            wr16(maxp, 28, 8);  // maxComponentElements
            wr16(maxp, 30, 8);  // maxComponentDepth
        }

        Bytes& hmtx = f.table(tag("hmtx"));
        for (uint32_t g = 0; g < count; ++g) {
            put16(hmtx, static_cast<uint32_t>(advance));
            put16(hmtx, 0);
        }

        f.table(tag("cmap")) = cmap_format12(cmap);

        // An empty name table and a version 3 post: present, and nothing in them.
        Bytes& name = f.table(tag("name"));
        put16(name, 0);
        put16(name, 0);
        put16(name, 6);
        Bytes& post = f.table(tag("post"));
        post.assign(32, 0);
        wr32(post, 0, 0x00030000u);

        if (is_cff) {
            f.table(tag("CFF ")) = cff;
        } else {
            Bytes& glyf = f.table(tag("glyf"));
            Bytes& loca = f.table(tag("loca"));
            for (const Bytes& g : glyphs) {
                if (long_loca)
                    put32(loca, static_cast<uint32_t>(glyf.size()));
                else
                    put16(loca, static_cast<uint32_t>(glyf.size() / 2));
                append(glyf, g);
                while (glyf.size() % 4 != 0)
                    glyf.push_back(0);
            }
            if (long_loca)
                put32(loca, static_cast<uint32_t>(glyf.size()));
            else
                put16(loca, static_cast<uint32_t>(glyf.size() / 2));
        }
        return f;
    }

    Bytes build() const { return sfnt().build(); }
};

// A glyf font with .notdef, 'A' and 'B' as squares: the base of the cases
// that then damage one thing.
inline MiniFont three_squares()
{
    MiniFont f;
    f.glyphs = {square_glyph(50, 0, 400), square_glyph(100, 0, 500), square_glyph(0, -100, 300)};
    f.cmap = {{'A', 'B', 1}};
    return f;
}

// --- CFF written from scratch ----------------------------------------------------------

inline Bytes cff_index(const std::vector<Bytes>& objects)
{
    Bytes out;
    put16(out, static_cast<uint32_t>(objects.size()));
    if (objects.empty())
        return out;
    put8(out, 4);
    uint32_t at = 1;
    put32(out, at);
    for (const Bytes& o : objects) {
        at += static_cast<uint32_t>(o.size());
        put32(out, at);
    }
    for (const Bytes& o : objects)
        append(out, o);
    return out;
}

// A DICT operand in the five-byte form, so offsets can be patched in place.
inline void dict_int(Bytes& out, uint32_t value)
{
    put8(out, 29);
    put32(out, value);
}

// A charstring operand in the three-byte form.
inline void cs_int(Bytes& out, int value)
{
    put8(out, 28);
    put16(out, static_cast<uint32_t>(value));
}

// What a subroutine index of `count` entries subtracts from a call's operand.
inline int cs_bias(size_t count)
{
    return count < 1240 ? 107 : count < 33900 ? 1131 : 32768;
}

// rmoveto, then a square, then endchar.
inline Bytes cs_square(int x, int y, int side)
{
    Bytes out;
    cs_int(out, x);
    cs_int(out, y);
    put8(out, 21);
    cs_int(out, side);
    put8(out, 6); // hlineto
    cs_int(out, side);
    put8(out, 7); // vlineto
    cs_int(out, -side);
    put8(out, 6);
    put8(out, 14);
    return out;
}

struct MiniCff {
    std::vector<Bytes> charstrings;
    std::vector<Bytes> global_subrs;
    std::vector<Bytes> local_subrs;
    // Appended to the Top DICT after the operators every font has.
    Bytes extra_top;

    Bytes build() const
    {
        Bytes head = {1, 0, 4, 4};
        const Bytes names = cff_index({Bytes{'M', 'i', 'n', 'i'}});
        const Bytes strings = cff_index({});
        const Bytes gsubrs = cff_index(global_subrs);
        const Bytes chars = cff_index(charstrings);
        const Bytes lsubrs = cff_index(local_subrs);
        Bytes priv;
        if (!local_subrs.empty()) {
            dict_int(priv, 6); // Subrs sits right after this DICT
            put8(priv, 19);
        } else {
            dict_int(priv, 0); // defaultWidthX, so the DICT is not empty
            put8(priv, 20);
        }
        // The Top DICT's size does not depend on the offsets in it.
        const size_t top_size = 5 + 1 + 10 + 1 + extra_top.size();
        const size_t top_index_size = 2 + 1 + 8 + top_size;
        const size_t chars_at =
            head.size() + names.size() + top_index_size + strings.size() + gsubrs.size();
        const size_t priv_at = chars_at + chars.size();
        Bytes top;
        dict_int(top, static_cast<uint32_t>(chars_at));
        put8(top, 17);
        dict_int(top, static_cast<uint32_t>(priv.size()));
        dict_int(top, static_cast<uint32_t>(priv_at));
        put8(top, 18);
        append(top, extra_top);
        Bytes out = head;
        append(out, names);
        append(out, cff_index({top}));
        append(out, strings);
        append(out, gsubrs);
        append(out, chars);
        append(out, priv);
        if (!local_subrs.empty())
            append(out, lsubrs);
        return out;
    }
};

inline MiniFont cff_font(const MiniCff& cff)
{
    MiniFont f;
    f.cff = cff.build();
    f.glyph_count = static_cast<uint32_t>(cff.charstrings.size());
    if (f.glyph_count > 1)
        f.cmap = {{'A', 'A' + f.glyph_count - 2, 1}};
    return f;
}

// --- Loading and measuring ---------------------------------------------------------------

inline std::string font_path(const std::string& name)
{
    return golden::data_path("fonts/" + name);
}

inline const char* const BUNDLED[] = {
    "Roboto-Regular.ttf",    "Roboto-Bold.ttf",        "Roboto-RegularItalic.ttf",
    "Roboto-BoldItalic.ttf", "Inconsolata-Medium.ttf", "DroidSans.ttf",
    "latinmodern-math.otf",
};

inline bool read_font(const char* name, Bytes& out)
{
    return golden::read_file(font_path(name), out);
}

inline double seconds_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// The process's peak resident size so far, in megabytes. ru_maxrss is in
// kilobytes on Linux and in bytes on macOS.
inline double peak_rss_mb()
{
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
}

// --- GPOS written from scratch ----------------------------------------------------------

// A GPOS whose one 'kern' lookup is an Extension lookup of `copies` subtable
// records, each leading to a PairPos format 2 subtable of class1 x class2
// cells of one XAdvance. Shared: they all lead to the same subtable. Not
// shared: to `copies` subtable headers 16 bytes apart, so that every matrix
// lies across the headers after it and they all end in the same bytes.
inline Bytes gpos_class_bomb(size_t copies, bool shared, uint32_t class1, uint32_t class2)
{
    Bytes out;
    put32(out, 0x00010000u);
    put16(out, 10); // ScriptList
    put16(out, 12); // FeatureList
    put16(out, 26); // LookupList
    put16(out, 0);  // no scripts
    put16(out, 1);  // one feature
    put32(out, tag("kern"));
    put16(out, 8);
    put16(out, 0);
    put16(out, 1);
    put16(out, 0);
    put16(out, 1); // one lookup
    put16(out, 4);
    put16(out, 9);
    put16(out, 0);
    put16(out, static_cast<uint32_t>(copies));
    for (size_t i = 0; i < copies; ++i)
        put16(out, static_cast<uint32_t>(6 + 2 * copies + 8 * i));
    const size_t headers = out.size() + 8 * copies;
    const size_t header_count = shared ? 1 : copies;
    for (size_t i = 0; i < copies; ++i) {
        const size_t here = out.size();
        put16(out, 1);
        put16(out, 2);
        put32(out, static_cast<uint32_t>(headers + (shared ? 0 : i * 16) - here));
    }
    for (size_t j = 0; j < header_count; ++j) {
        const uint32_t aux = static_cast<uint32_t>((header_count - j) * 16);
        put16(out, 2);
        put16(out, aux);    // coverage
        put16(out, 0x0004); // XAdvance
        put16(out, 0);
        put16(out, aux + 6); // ClassDef1
        put16(out, aux + 6); // ClassDef2
        put16(out, class1);
        put16(out, class2);
    }
    put16(out, 1); // coverage format 1: glyph 36
    put16(out, 1);
    put16(out, 36);
    put16(out, 1); // class definition format 1, no glyphs
    put16(out, 0);
    put16(out, 0);
    out.resize(out.size() + size_t(class1) * class2 * 2, 0);
    return out;
}

// PairPos format 1: one pair, one XAdvance.
inline Bytes pairpos1(uint32_t first, uint32_t second, int value)
{
    Bytes out;
    put16(out, 1);
    put16(out, 18); // coverage
    put16(out, 0x0004);
    put16(out, 0);
    put16(out, 1);
    put16(out, 12);
    put16(out, 1); // the pair set
    put16(out, second);
    put16(out, static_cast<uint32_t>(value));
    put16(out, 1); // coverage format 1
    put16(out, 1);
    put16(out, first);
    return out;
}

// A GPOS with one 'kern' feature naming `feature_lookups`, and one PairPos
// lookup per entry of `subtables`.
inline Bytes gpos_assemble(const std::vector<uint32_t>& feature_lookups,
                           const std::vector<Bytes>& subtables)
{
    Bytes out;
    const size_t lookup_list = 12 + 2 + 6 + 4 + 2 * feature_lookups.size();
    put32(out, 0x00010000u);
    put16(out, 10);
    put16(out, 12);
    put16(out, static_cast<uint32_t>(lookup_list));
    put16(out, 0);
    put16(out, 1);
    put32(out, tag("kern"));
    put16(out, 8);
    put16(out, 0);
    put16(out, static_cast<uint32_t>(feature_lookups.size()));
    for (const uint32_t index : feature_lookups)
        put16(out, index);
    put16(out, static_cast<uint32_t>(subtables.size()));
    size_t at = 2 + 2 * subtables.size();
    for (const Bytes& sub : subtables) {
        put16(out, static_cast<uint32_t>(at));
        at += 8 + sub.size();
    }
    for (const Bytes& sub : subtables) {
        put16(out, 2);
        put16(out, 0);
        put16(out, 1);
        put16(out, 8);
        append(out, sub);
    }
    return out;
}

inline Bytes with_table(const char* font, uint32_t which, const Bytes& data)
{
    Bytes file;
    Sfnt sfnt;
    if (!read_font(font, file) || !parse_sfnt(file, sfnt))
        return {};
    sfnt.table(which) = data;
    return sfnt.build();
}

inline bool warned(const std::vector<std::string>& warnings, const char* part)
{
    for (const std::string& w : warnings)
        if (w.find(part) != std::string::npos)
            return true;
    return false;
}

// A MATH table: zeroed constants, and one vertical construction for glyph
// `base` whose variants are `variants`.
inline Bytes math_table(uint32_t base, const std::vector<uint32_t>& variants)
{
    Bytes out;
    put32(out, 0x00010000u);
    put16(out, 10);  // MathConstants
    put16(out, 0);   // no MathGlyphInfo
    put16(out, 224); // MathVariants
    out.resize(224, 0);
    wr16(out, 10, 70); // ScriptPercentScaleDown
    wr16(out, 12, 50);
    put16(out, 20); // minConnectorOverlap
    put16(out, 12); // vertical coverage
    put16(out, 0);
    put16(out, 1);
    put16(out, 0);
    put16(out, 18); // the construction
    put16(out, 1);  // coverage format 1
    put16(out, 1);
    put16(out, base);
    put16(out, 0); // no assembly
    put16(out, static_cast<uint32_t>(variants.size()));
    for (size_t i = 0; i < variants.size(); ++i) {
        put16(out, variants[i]);
        put16(out, static_cast<uint32_t>(600 + 200 * i));
    }
    return out;
}

// 'A' is glyph 1; glyphs 2..6 are its vertical variants and have no
// codepoint, so five codepoints are minted.
inline MiniFont mini_math()
{
    MiniFont f;
    f.glyphs.push_back(square_glyph(50, 0, 400));
    for (int i = 0; i < 6; ++i)
        f.glyphs.push_back(square_glyph(0, -100, 300 + 60 * i));
    f.cmap = {{'A', 'A', 1}};
    return f;
}

inline Bytes mini_math_file(const MiniFont& f, const Bytes& math)
{
    Sfnt sfnt = f.sfnt();
    sfnt.table(tag("MATH")) = math;
    return sfnt.build();
}

// --- cmap subtables, and a cmap table of any records ------------------------------------

struct CmapRecord {
    uint32_t platform = 0, encoding = 0;
    Bytes subtable;
};

// The records in the order given (a font's are sorted; the reader does not
// need them to be), each followed in the table by its own subtable.
inline Bytes cmap_table(const std::vector<CmapRecord>& records)
{
    Bytes out;
    put16(out, 0);
    put16(out, static_cast<uint32_t>(records.size()));
    size_t at = 4 + records.size() * 8;
    for (const CmapRecord& r : records) {
        put16(out, r.platform);
        put16(out, r.encoding);
        put32(out, static_cast<uint32_t>(at));
        at += r.subtable.size();
    }
    for (const CmapRecord& r : records)
        append(out, r.subtable);
    return out;
}

// Format 0: 256 glyph ids of one byte each, 262 bytes in all.
inline Bytes cmap_sub0(const std::vector<std::array<uint32_t, 2>>& code_glyph)
{
    Bytes out;
    put16(out, 0);
    put16(out, 262);
    put16(out, 0);
    out.resize(262, 0);
    for (const auto& e : code_glyph)
        out[6 + (e[0] & 0xFFu)] = static_cast<uint8_t>(e[1]);
    return out;
}

// Format 4 with one segment per entry plus the terminator; idDelta only.
inline Bytes cmap_sub4(const std::vector<CmapGroup>& segments)
{
    const size_t n = segments.size() + 1;
    Bytes sub;
    put16(sub, 4);
    put16(sub, static_cast<uint32_t>(16 + n * 8));
    put16(sub, 0);
    put16(sub, static_cast<uint32_t>(n * 2));
    put16(sub, 0);
    put16(sub, 0);
    put16(sub, 0);
    for (const CmapGroup& s : segments)
        put16(sub, s.last);
    put16(sub, 0xFFFF);
    put16(sub, 0);
    for (const CmapGroup& s : segments)
        put16(sub, s.first);
    put16(sub, 0xFFFF);
    for (const CmapGroup& s : segments)
        put16(sub, (s.glyph - s.first) & 0xFFFFu);
    put16(sub, 1);
    for (size_t i = 0; i < n; ++i)
        put16(sub, 0);
    return sub;
}

// Format 6: a trimmed array of 16-bit codes from `first`.
inline Bytes cmap_sub6(uint32_t first, const std::vector<uint32_t>& glyphs)
{
    Bytes out;
    put16(out, 6);
    put16(out, static_cast<uint32_t>(10 + glyphs.size() * 2));
    put16(out, 0);
    put16(out, first);
    put16(out, static_cast<uint32_t>(glyphs.size()));
    for (const uint32_t g : glyphs)
        put16(out, g);
    return out;
}

// Format 10: a trimmed array of 32-bit codes from `first`.
inline Bytes cmap_sub10(uint32_t first, const std::vector<uint32_t>& glyphs)
{
    Bytes out;
    put16(out, 10);
    put16(out, 0);
    put32(out, static_cast<uint32_t>(20 + glyphs.size() * 2));
    put32(out, 0);
    put32(out, first);
    put32(out, static_cast<uint32_t>(glyphs.size()));
    for (const uint32_t g : glyphs)
        put16(out, g);
    return out;
}

// Formats 12 and 13: groups of (first code, last code, glyph).
inline Bytes cmap_sub12or13(uint32_t format, const std::vector<CmapGroup>& groups)
{
    Bytes out;
    put16(out, format);
    put16(out, 0);
    put32(out, static_cast<uint32_t>(16 + groups.size() * 12));
    put32(out, 0);
    put32(out, static_cast<uint32_t>(groups.size()));
    for (const CmapGroup& g : groups) {
        put32(out, g.first);
        put32(out, g.last);
        put32(out, g.glyph);
    }
    return out;
}

// A subtable in a format the reader passes over (2, 8 or 14): the format
// word and enough zeros that nothing about it is truncated.
inline Bytes cmap_sub_unread(uint32_t format)
{
    Bytes out;
    put16(out, format);
    out.resize(64, 0);
    return out;
}

// --- A collection --------------------------------------------------------------------

// A version 1.0 'ttcf' file of whole fonts: the header and the offsets, then
// each face's own directory and tables, the table offsets counted from the
// start of the file as a collection's are.
inline Bytes collection(const std::vector<Bytes>& faces)
{
    Bytes out;
    put32(out, tag("ttcf"));
    put16(out, 1);
    put16(out, 0);
    put32(out, static_cast<uint32_t>(faces.size()));
    size_t at = 12 + faces.size() * 4;
    std::vector<size_t> starts;
    for (const Bytes& face : faces) {
        starts.push_back(at);
        put32(out, static_cast<uint32_t>(at));
        at += (face.size() + 3) & ~size_t(3);
    }
    for (size_t i = 0; i < faces.size(); ++i) {
        Bytes face = faces[i];
        const size_t count = rd16(face, 4);
        for (size_t t = 0; t < count; ++t)
            wr32(face, 12 + t * 16 + 8,
                 rd32(face, 12 + t * 16 + 8) + static_cast<uint32_t>(starts[i]));
        append(out, face);
        while (out.size() % 4 != 0)
            out.push_back(0);
    }
    return out;
}

// --- post ----------------------------------------------------------------------------

// A post table: the 32-byte header, and for version 2 the glyph count, a name
// index per glyph and the Pascal strings.
inline Bytes post_table(uint32_t version, const std::vector<uint32_t>& indices = {},
                        const std::vector<std::string>& strings = {})
{
    Bytes out(32, 0);
    wr32(out, 0, version);
    if (version != 0x00020000u)
        return out;
    put16(out, static_cast<uint32_t>(indices.size()));
    for (const uint32_t index : indices)
        put16(out, index);
    for (const std::string& s : strings) {
        put8(out, static_cast<uint32_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
    return out;
}

// --- The 'kern' table ------------------------------------------------------------------

struct KernPair {
    uint32_t left, right;
    int value;
};

// One subtable: version, length, a FORMAT byte, a COVERAGE byte, then for
// format 0 the pair count, three search fields and six bytes a pair.
// `length` below zero writes the true length (modulo 65536, which is all the
// field holds).
inline Bytes kern_subtable(const std::vector<KernPair>& pairs, uint32_t coverage = 0x01,
                           uint32_t format = 0, int length = -1)
{
    Bytes out;
    const size_t size = 14 + pairs.size() * 6;
    put16(out, 0);
    put16(out, length < 0 ? static_cast<uint32_t>(size & 0xFFFFu) : static_cast<uint32_t>(length));
    put8(out, format);
    put8(out, coverage);
    put16(out, static_cast<uint32_t>(pairs.size()));
    put16(out, 0);
    put16(out, 0);
    put16(out, 0);
    for (const KernPair& p : pairs) {
        put16(out, p.left);
        put16(out, p.right);
        put16(out, static_cast<uint32_t>(p.value));
    }
    return out;
}

inline Bytes kern_table(const std::vector<Bytes>& subtables)
{
    Bytes out;
    put16(out, 0);
    put16(out, static_cast<uint32_t>(subtables.size()));
    for (const Bytes& s : subtables)
        append(out, s);
    return out;
}

// --- More GPOS -------------------------------------------------------------------------

// ClassDef format 2: ranges of (first glyph, last glyph, class), as given.
inline Bytes classdef2(const std::vector<std::array<uint32_t, 3>>& ranges)
{
    Bytes out;
    put16(out, 2);
    put16(out, static_cast<uint32_t>(ranges.size()));
    for (const auto& r : ranges) {
        put16(out, r[0]);
        put16(out, r[1]);
        put16(out, r[2]);
    }
    return out;
}

inline Bytes coverage1(const std::vector<uint32_t>& glyphs)
{
    Bytes out;
    put16(out, 1);
    put16(out, static_cast<uint32_t>(glyphs.size()));
    for (const uint32_t g : glyphs)
        put16(out, g);
    return out;
}

// PairPos format 2 with one XAdvance a cell: `matrix` is class1 rows of
// class2 values. An empty coverage or class definition is a null offset.
inline Bytes pairpos2(const std::vector<uint32_t>& covered, const Bytes& classes1,
                      const Bytes& classes2, const std::vector<std::vector<int>>& matrix)
{
    const size_t rows = matrix.size(), columns = rows == 0 ? 0 : matrix[0].size();
    const Bytes coverage = covered.empty() ? Bytes() : coverage1(covered);
    size_t at = 16 + rows * columns * 2;
    const auto offset_of = [&](const Bytes& part) {
        const size_t here = part.empty() ? 0 : at;
        at += part.size();
        return static_cast<uint32_t>(here);
    };
    Bytes out;
    put16(out, 2);
    put16(out, offset_of(coverage));
    put16(out, 0x0004);
    put16(out, 0);
    put16(out, offset_of(classes1));
    put16(out, offset_of(classes2));
    put16(out, static_cast<uint32_t>(rows));
    put16(out, static_cast<uint32_t>(columns));
    for (const std::vector<int>& row : matrix)
        for (const int v : row)
            put16(out, static_cast<uint32_t>(v));
    append(out, coverage);
    append(out, classes1);
    append(out, classes2);
    return out;
}

struct PairSet {
    uint32_t first = 0;
    std::vector<std::array<int, 2>> pairs; // (second glyph, XAdvance)
    bool null_offset = false;              // the set's offset is written as 0
};

// PairPos format 1 of several first glyphs, in the order given (which must
// ascend, as coverage does).
inline Bytes pairpos1_sets(const std::vector<PairSet>& sets)
{
    Bytes out;
    put16(out, 1);
    const size_t coverage_at = out.size();
    put16(out, 0);
    put16(out, 0x0004);
    put16(out, 0);
    put16(out, static_cast<uint32_t>(sets.size()));
    const size_t offsets_at = out.size();
    out.resize(out.size() + sets.size() * 2, 0);
    std::vector<uint32_t> firsts;
    for (size_t i = 0; i < sets.size(); ++i) {
        firsts.push_back(sets[i].first);
        if (!sets[i].null_offset)
            wr16(out, offsets_at + i * 2, static_cast<uint32_t>(out.size()));
        put16(out, static_cast<uint32_t>(sets[i].pairs.size()));
        for (const auto& p : sets[i].pairs) {
            put16(out, static_cast<uint32_t>(p[0]));
            put16(out, static_cast<uint32_t>(p[1]));
        }
    }
    wr16(out, coverage_at, static_cast<uint32_t>(out.size()));
    append(out, coverage1(firsts));
    return out;
}

// A GPOS with one 'kern' feature naming `feature_lookups`, and one PairPos
// lookup per entry of `lookups`, each of that entry's subtables.
inline Bytes gpos_lookups(const std::vector<uint32_t>& feature_lookups,
                          const std::vector<std::vector<Bytes>>& lookups)
{
    Bytes out;
    const size_t lookup_list = 12 + 2 + 6 + 4 + 2 * feature_lookups.size();
    put32(out, 0x00010000u);
    put16(out, 10);
    put16(out, 12);
    put16(out, static_cast<uint32_t>(lookup_list));
    put16(out, 0);
    put16(out, 1);
    put32(out, tag("kern"));
    put16(out, 8);
    put16(out, 0);
    put16(out, static_cast<uint32_t>(feature_lookups.size()));
    for (const uint32_t index : feature_lookups)
        put16(out, index);
    put16(out, static_cast<uint32_t>(lookups.size()));
    size_t at = 2 + 2 * lookups.size();
    for (const std::vector<Bytes>& subtables : lookups) {
        put16(out, static_cast<uint32_t>(at));
        at += 6 + 2 * subtables.size();
        for (const Bytes& sub : subtables)
            at += sub.size();
    }
    for (const std::vector<Bytes>& subtables : lookups) {
        put16(out, 2);
        put16(out, 0);
        put16(out, static_cast<uint32_t>(subtables.size()));
        size_t sub_at = 6 + 2 * subtables.size();
        for (const Bytes& sub : subtables) {
            put16(out, static_cast<uint32_t>(sub_at));
            sub_at += sub.size();
        }
        for (const Bytes& sub : subtables)
            append(out, sub);
    }
    return out;
}

// --- More charstrings ------------------------------------------------------------------

// An operator, and one of the two-byte operators (escape 12).
inline void cs_op(Bytes& out, uint32_t op)
{
    put8(out, op);
}

inline void cs_esc(Bytes& out, uint32_t op)
{
    put8(out, 12);
    put8(out, op);
}

// A subroutine: a square from the current point, then return.
inline Bytes subr_square(int side)
{
    Bytes out;
    cs_int(out, side);
    put8(out, 6); // hlineto
    cs_int(out, side);
    put8(out, 7); // vlineto
    cs_int(out, -side);
    put8(out, 6);
    put8(out, 11);
    return out;
}

// rmoveto, a call of subroutine `index` of `count` (10 local, 29 global), endchar.
inline Bytes cs_call(int x, int y, int index, size_t count, uint32_t call = 10)
{
    Bytes out;
    cs_int(out, x);
    cs_int(out, y);
    put8(out, 21);
    cs_int(out, index - cs_bias(count));
    put8(out, call);
    put8(out, 14);
    return out;
}

// A charstring from text: integers and decimals are operands (a decimal is
// written in the 16.16 form, byte 255), names are operators.
inline Bytes cs_asm(const char* text)
{
    struct Op {
        const char* name;
        int code; // 1200 + n for the escaped ones
    };
    static const Op ops[] = {
        {"hstem", 1},     {"vstem", 3},     {"vmoveto", 4},    {"rlineto", 5},  {"hlineto", 6},
        {"vlineto", 7},   {"rrcurveto", 8}, {"callsubr", 10},  {"return", 11},  {"endchar", 14},
        {"rmoveto", 21},  {"hmoveto", 22},  {"callgsubr", 29}, {"and", 1203},   {"or", 1204},
        {"not", 1205},    {"abs", 1209},    {"add", 1210},     {"sub", 1211},   {"div", 1212},
        {"neg", 1214},    {"eq", 1215},     {"drop", 1218},    {"put", 1220},   {"get", 1221},
        {"ifelse", 1222}, {"random", 1223}, {"mul", 1224},     {"sqrt", 1226},  {"dup", 1227},
        {"exch", 1228},   {"index", 1229},  {"roll", 1230},    {"hflex", 1234}, {"flex", 1235},
        {"hflex1", 1236}, {"flex1", 1237},
    };
    Bytes out;
    const char* p = text;
    while (*p != '\0') {
        while (*p == ' ')
            ++p;
        const char* end = p;
        while (*end != '\0' && *end != ' ')
            ++end;
        const std::string word(p, end);
        p = end;
        if (word.empty())
            continue;
        const char first = word[0];
        if (first == '-' || (first >= '0' && first <= '9')) {
            if (word.find('.') == std::string::npos) {
                cs_int(out, std::atoi(word.c_str()));
            } else {
                put8(out, 255);
                put32(out, static_cast<uint32_t>(
                               static_cast<int32_t>(std::strtod(word.c_str(), nullptr) * 65536.0)));
            }
            continue;
        }
        for (const Op& op : ops) {
            if (word != op.name)
                continue;
            if (op.code >= 1200)
                cs_esc(out, static_cast<uint32_t>(op.code - 1200));
            else
                cs_op(out, static_cast<uint32_t>(op.code));
        }
    }
    return out;
}

// --- A CID-keyed CFF -------------------------------------------------------------------

// FDSelect format 0: a Font DICT per glyph.
inline Bytes fdselect0(const std::vector<uint32_t>& fds)
{
    Bytes out;
    put8(out, 0);
    for (const uint32_t fd : fds)
        put8(out, fd);
    return out;
}

// FDSelect format 3: (first glyph, Font DICT) ranges and the closing glyph id.
inline Bytes fdselect3(const std::vector<std::array<uint32_t, 2>>& ranges, uint32_t sentinel)
{
    Bytes out;
    put8(out, 3);
    put16(out, static_cast<uint32_t>(ranges.size()));
    for (const auto& r : ranges) {
        put16(out, r[0]);
        put8(out, r[1]);
    }
    put16(out, sentinel);
    return out;
}

// A CID-keyed CFF table: ROS, charset (format 0, of `cids`), CharStrings,
// FDArray and FDSelect in the Top DICT and no Private there; one Font DICT
// per entry of `fd_subrs`, each with a Private DICT of its own that names its
// own local Subrs INDEX (none if the entry is empty). Every DICT operand is
// in the five-byte form. The order in the table: header, Name INDEX, Top DICT
// INDEX, String INDEX, Global Subr INDEX, charset, FDSelect, CharStrings,
// FDArray, the Private DICTs, the Subrs INDEXes.
struct CidCff {
    std::vector<Bytes> charstrings;
    std::vector<Bytes> global_subrs;
    std::vector<std::vector<Bytes>> fd_subrs;
    Bytes fd_select;
    std::vector<uint32_t> cids; // for glyph 1 on; empty for no charset operator
    bool with_fd_array = true;
    bool with_fd_select = true;
    // Every Private DICT names the first Font DICT's Subrs INDEX.
    bool share_subrs = false;
    // In place of the Font DICT INDEX built from fd_subrs, if not empty.
    Bytes fd_array_override;

    // Where the parts landed in the last table built.
    mutable size_t fd_array_at = 0, fd_select_at = 0, charstrings_at = 0, privates_at = 0;
    mutable std::vector<size_t> subrs_at;

    Bytes build() const
    {
        const Bytes head = {1, 0, 4, 4};
        const Bytes names = cff_index({Bytes{'M', 'i', 'n', 'i'}});
        const Bytes strings = cff_index(
            {Bytes{'A', 'd', 'o', 'b', 'e'}, Bytes{'I', 'd', 'e', 'n', 't', 'i', 't', 'y'}});
        const Bytes gsubrs = cff_index(global_subrs);
        const Bytes chars = cff_index(charstrings);
        Bytes charset;
        if (!cids.empty()) {
            put8(charset, 0);
            for (const uint32_t cid : cids)
                put16(charset, cid);
        }
        const size_t fds = fd_subrs.size();
        const size_t top_size =
            17 + (cids.empty() ? 0 : 6) + 6 + (with_fd_array ? 7 : 0) + (with_fd_select ? 7 : 0);
        const size_t top_index_size = 2 + 1 + 8 + top_size;
        const size_t charset_at =
            head.size() + names.size() + top_index_size + strings.size() + gsubrs.size();
        fd_select_at = charset_at + charset.size();
        charstrings_at = fd_select_at + fd_select.size();
        fd_array_at = charstrings_at + chars.size();
        // A Font DICT is `size offset Private`, 11 bytes; a Private DICT is
        // one operand and one operator, 6 bytes.
        const size_t fd_array_size = !fd_array_override.empty() ? fd_array_override.size()
                                                                : 2 + 1 + 4 * (fds + 1) + 11 * fds;
        privates_at = fd_array_at + fd_array_size;
        std::vector<Bytes> subr_indexes;
        subrs_at.clear();
        size_t at = privates_at + 6 * fds;
        for (size_t fd = 0; fd < fds; ++fd) {
            subr_indexes.push_back(fd_subrs[fd].empty() ? Bytes() : cff_index(fd_subrs[fd]));
            subrs_at.push_back(fd_subrs[fd].empty() ? 0 : at);
            at += subr_indexes.back().size();
        }
        std::vector<Bytes> font_dicts;
        Bytes privates;
        for (size_t fd = 0; fd < fds; ++fd) {
            const size_t private_at = privates_at + 6 * fd;
            Bytes dict;
            dict_int(dict, 6);
            dict_int(dict, static_cast<uint32_t>(private_at));
            put8(dict, 18);
            font_dicts.push_back(dict);
            const size_t named = share_subrs ? subrs_at[0] : subrs_at[fd];
            if (named != 0) {
                dict_int(privates, static_cast<uint32_t>(named - private_at));
                put8(privates, 19);
            } else {
                dict_int(privates, 0); // defaultWidthX, so the DICT is not empty
                put8(privates, 20);
            }
        }
        Bytes top;
        dict_int(top, 391); // "Adobe"
        dict_int(top, 392); // "Identity"
        dict_int(top, 0);
        put8(top, 12);
        put8(top, 30); // ROS
        if (!cids.empty()) {
            dict_int(top, static_cast<uint32_t>(charset_at));
            put8(top, 15);
        }
        dict_int(top, static_cast<uint32_t>(charstrings_at));
        put8(top, 17);
        if (with_fd_array) {
            dict_int(top, static_cast<uint32_t>(fd_array_at));
            put8(top, 12);
            put8(top, 36);
        }
        if (with_fd_select) {
            dict_int(top, static_cast<uint32_t>(fd_select_at));
            put8(top, 12);
            put8(top, 37);
        }
        Bytes out = head;
        append(out, names);
        append(out, cff_index({top}));
        append(out, strings);
        append(out, gsubrs);
        append(out, charset);
        append(out, fd_select);
        append(out, chars);
        append(out, !fd_array_override.empty() ? fd_array_override : cff_index(font_dicts));
        append(out, privates);
        for (const Bytes& index : subr_indexes)
            append(out, index);
        return out;
    }
};

// Five glyphs: .notdef, then four that each call local subroutine 0 from
// (10, 20). Font DICT 0's subroutine 0 is a square of 100, Font DICT 1's a
// square of 300. FDSelect is left to the caller.
inline CidCff two_dict_cid()
{
    CidCff cff;
    cff.charstrings.push_back(cs_square(0, 0, 50));
    for (int g = 1; g < 5; ++g)
        cff.charstrings.push_back(cs_call(10, 20, 0, 1));
    cff.fd_subrs = {{subr_square(100)}, {subr_square(300)}};
    cff.cids = {100, 7, 65535, 1};
    return cff;
}

inline MiniFont cid_font(const CidCff& cff)
{
    MiniFont f;
    f.cff = cff.build();
    f.glyph_count = static_cast<uint32_t>(cff.charstrings.size());
    if (f.glyph_count > 1)
        f.cmap = {{'A', 'A' + f.glyph_count - 2, 1}};
    return f;
}

} // namespace font_builders
