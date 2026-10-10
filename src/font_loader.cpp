#include "font_loader.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>

namespace ImTypeset::detail
{

namespace
{

uint32_t be32(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

void put16(std::vector<uint8_t>& out, size_t at, uint32_t value)
{
    out[at] = static_cast<uint8_t>(value >> 8);
    out[at + 1] = static_cast<uint8_t>(value);
}

void put32(std::vector<uint8_t>& out, size_t at, uint32_t value)
{
    out[at] = static_cast<uint8_t>(value >> 24);
    out[at + 1] = static_cast<uint8_t>(value >> 16);
    out[at + 2] = static_cast<uint8_t>(value >> 8);
    out[at + 3] = static_cast<uint8_t>(value);
}

constexpr uint32_t tag(char a, char b, char c, char d)
{
    return (uint32_t(static_cast<uint8_t>(a)) << 24) | (uint32_t(static_cast<uint8_t>(b)) << 16) |
           (uint32_t(static_cast<uint8_t>(c)) << 8) | uint32_t(static_cast<uint8_t>(d));
}

// The OpenType table checksum: the sum of the big-endian 32-bit words of
// `length` bytes at `at`, the last word padded with zeros.
uint32_t checksum(const std::vector<uint8_t>& bytes, size_t at, size_t length)
{
    uint32_t sum = 0;
    size_t i = 0;
    for (; i + 4 <= length; i += 4)
        sum += be32(bytes.data() + at + i);
    if (i < length) {
        uint8_t last[4] = {0, 0, 0, 0};
        std::memcpy(last, bytes.data() + at + i, length - i);
        sum += be32(last);
    }
    return sum;
}

struct Group {
    uint32_t first, last, glyph;
    // Every code has `glyph`, as in the reader's CmapRange.
    bool one_glyph;
};

// The cmap subtable of the font the atlas is given.
struct Groups {
    // 12, glyph ids running on through a group, or 13, one glyph a group:
    // whichever the face's own ranges are, so that a font whose cmap gives a
    // glyph to whole blocks of codes is not written out a group a code.
    int format = 12;
    std::vector<Group> groups;
};

// Every codepoint the atlas may be asked for: ascending, not overlapping,
// every glyph id inside the font.
Groups cmap_groups(const FaceData& face, int wchar_bits)
{
    const OpenTypeFont& font = face.font();
    const uint32_t glyphs = font.glyph_count();
    Groups out;
    std::vector<Group> groups;
    for (const CmapRange& range : font.cmap()) {
        // A codepoint the file maps past its last glyph draws nothing and is
        // left out, so the table is one any rasteriser's validation accepts.
        if (range.first > 0x10FFFFu || range.glyph >= glyphs)
            continue;
        const uint32_t by_codepoint = std::min(range.last, 0x10FFFFu) - range.first;
        const uint32_t by_glyph = range.one_glyph ? by_codepoint : glyphs - 1 - range.glyph;
        const uint32_t last = range.first + std::min(by_codepoint, by_glyph);
        // A run of one code is the same thing either way.
        const bool one_glyph = range.one_glyph && last > range.first;
        if (one_glyph)
            out.format = 13;
        groups.push_back({range.first, last, range.glyph, one_glyph});
    }
    for (const SyntheticCodepoint& minted : face.synthetic_codepoints())
        if (minted.glyph < glyphs)
            groups.push_back({minted.codepoint, minted.codepoint, minted.glyph, false});
    if (wchar_bits < 32) {
        for (const auto& stand_in : face.atlas_stand_ins()) {
            const uint32_t glyph = face.glyph(stand_in.second);
            if (glyph < glyphs)
                groups.push_back({stand_in.first, stand_in.first, glyph, false});
        }
    }
    std::sort(groups.begin(), groups.end(),
              [](const Group& a, const Group& b) { return a.first < b.first; });
    // Minted codepoints and stand-ins are chosen where the font maps nothing,
    // so nothing overlaps; a later group that did would be dropped here.
    std::vector<Group>& merged = out.groups;
    const auto add = [&](const Group& g) {
        const bool joins =
            !merged.empty() && g.first == merged.back().last + 1 &&
            (out.format == 13 ? g.glyph == merged.back().glyph
                              : g.glyph == merged.back().glyph + (g.first - merged.back().first));
        if (joins)
            merged.back().last = g.last;
        else
            merged.push_back(g);
    };
    for (const Group& g : groups) {
        if (!merged.empty() && g.first <= merged.back().last)
            continue;
        if (out.format == 13 && !g.one_glyph && g.last > g.first) {
            // The reader's ranges are all of one kind, a subtable being of
            // one format, so this is not reached from a font it read; a run
            // of consecutive glyphs in a format 13 table is a group a code.
            for (uint32_t cp = g.first;; ++cp) {
                add({cp, cp, g.glyph + (cp - g.first), false});
                if (cp == g.last)
                    break;
            }
        } else {
            add(g);
        }
    }
    return out;
}

std::vector<uint8_t> cmap_table(const Groups& cmap)
{
    const std::vector<Group>& groups = cmap.groups;
    // Header (version, one encoding record), then the format 12 or 13
    // subtable; the two are laid out alike.
    const size_t subtable = 12, header = 16;
    std::vector<uint8_t> out(subtable + header + groups.size() * 12);
    put16(out, 0, 0);  // version
    put16(out, 2, 1);  // numTables
    put16(out, 4, 3);  // platform: Windows
    put16(out, 6, 10); // encoding: Unicode full repertoire
    put32(out, 8, static_cast<uint32_t>(subtable));
    put16(out, subtable, static_cast<uint32_t>(cmap.format));                     // format
    put16(out, subtable + 2, 0);                                                  // reserved
    put32(out, subtable + 4, static_cast<uint32_t>(header + groups.size() * 12)); // length
    put32(out, subtable + 8, 0);                                                  // language
    put32(out, subtable + 12, static_cast<uint32_t>(groups.size()));
    for (size_t i = 0; i < groups.size(); ++i) {
        const size_t at = subtable + header + i * 12;
        put32(out, at, groups[i].first);
        put32(out, at + 4, groups[i].last);
        put32(out, at + 8, groups[i].glyph);
    }
    return out;
}

} // namespace

bool build_atlas_font(const FaceData& face, int wchar_bits, std::vector<uint8_t>& out,
                      std::string* why)
{
    out.clear();
    const OpenTypeFont& font = face.font();
    const size_t file_size = face.byte_count();

    struct Entry {
        uint32_t tag;
        const uint8_t* data;
        size_t length;
    };
    std::vector<Entry> tables;
    // The tables are the ones the reader took for this face: for a face of a
    // collection its own directory's, wherever in the file that is, and never
    // one that runs past the end of the file. What is written is a font of
    // that one face.
    //
    // Tables that share nothing add up to no more than the file. Ones that
    // overlap could make the copy many times the file's size, so they are
    // copied only while they stay within it.
    size_t copied = 0;
    for (size_t i = 0; i < font.table_count(); ++i) {
        const TableRecord record = font.table_record(i);
        if (record.tag == tag('c', 'm', 'a', 'p') || record.tag == tag('D', 'S', 'I', 'G'))
            continue;
        if (record.size > file_size - copied) {
            if (why != nullptr)
                *why = "the font's tables overlap";
            return false;
        }
        copied += record.size;
        tables.push_back({record.tag, record.data, record.size});
    }
    const std::vector<uint8_t> cmap = cmap_table(cmap_groups(face, wchar_bits));
    tables.push_back({tag('c', 'm', 'a', 'p'), cmap.data(), cmap.size()});
    std::sort(tables.begin(), tables.end(),
              [](const Entry& a, const Entry& b) { return a.tag < b.tag; });
    // The reader refuses such a file, so that layout and the atlas cannot
    // take different tables for one name; this holds it here as well.
    for (size_t i = 1; i < tables.size(); ++i) {
        if (tables[i].tag == tables[i - 1].tag) {
            if (why != nullptr)
                *why = "the table directory names a table twice";
            return false;
        }
    }

    size_t total = 12 + tables.size() * 16;
    for (const Entry& table : tables)
        total += (table.length + 3) & ~size_t(3);
    if (total > static_cast<size_t>(INT_MAX)) {
        if (why != nullptr)
            *why = "the font is too large for ImGui to be handed";
        return false;
    }
    out.assign(total, 0);
    put32(out, 0, font.sfnt_version()); // the face's, not a collection's 'ttcf'
    const uint32_t n = static_cast<uint32_t>(tables.size());
    uint32_t power = 1, log2 = 0;
    while (power * 2 <= n) {
        power *= 2;
        ++log2;
    }
    put16(out, 4, n);
    put16(out, 6, power * 16);           // searchRange
    put16(out, 8, log2);                 // entrySelector
    put16(out, 10, n * 16 - power * 16); // rangeShift

    size_t at = 12 + tables.size() * 16;
    size_t head_at = 0;
    for (size_t i = 0; i < tables.size(); ++i) {
        const Entry& table = tables[i];
        if (table.length > 0)
            std::memcpy(out.data() + at, table.data, table.length);
        if (table.tag == tag('h', 'e', 'a', 'd') && table.length >= 12) {
            head_at = at;
            put32(out, at + 8, 0); // checkSumAdjustment is zero while summing
        }
        // OS/2.fsSelection bit 7, USE_TYPO_METRICS, is cleared. With it set
        // FreeType sizes the font by OS/2's typographic ascender and
        // descender; layout and stb_truetype size it by hhea's, and where the
        // two differ the glyphs would be drawn at another size than they were
        // measured at.
        if (table.tag == tag('O', 'S', '/', '2') && table.length >= 64)
            out[at + 63] = static_cast<uint8_t>(out[at + 63] & 0x7Fu);
        // The glyf headers the strict pass found describing a box at or
        // below the baseline for an outline that rises above it: yMax is
        // raised over the outline, so that stb_truetype, which sizes the
        // bitmap by the header, has every edge inside it.
        if (table.tag == tag('g', 'l', 'y', 'f')) {
            for (const GlyphBoxRepair& repair : font.atlas_box_repairs())
                if (repair.glyf_offset <= table.length && table.length - repair.glyf_offset >= 10)
                    put16(out, at + repair.glyf_offset + 8, static_cast<uint16_t>(repair.y_max));
        }
        const size_t record = 12 + i * 16;
        put32(out, record, table.tag);
        put32(out, record + 4, checksum(out, at, table.length));
        put32(out, record + 8, static_cast<uint32_t>(at));
        put32(out, record + 12, static_cast<uint32_t>(table.length));
        at += (table.length + 3) & ~size_t(3);
    }
    if (head_at != 0)
        put32(out, head_at + 8, 0xB1B0AFBAu - checksum(out, 0, out.size()));
    return true;
}

ImFont* add_face_to_atlas(ImFontAtlas* atlas, const FaceData& face, float size_pixels,
                          const ImFontConfig* config, std::string* why)
{
    const auto refuse = [&](const std::string& reason) -> ImFont* {
        if (why != nullptr)
            *why = reason;
        return nullptr;
    };
    // What ImGui asserts on, asked first: none of it is to be reached with a
    // font file or a caller's configuration deciding whether it fires.
    if (atlas == nullptr)
        return refuse("there is no font atlas");
    if (atlas->Locked)
        return refuse("the font atlas is locked");
    if (config != nullptr && !(config->RasterizerDensity > 0.0f))
        return refuse("ImFontConfig::RasterizerDensity must be positive");
    // A rasteriser follows offsets the reader tolerated being wrong, and
    // stb_truetype checks none of them.
    std::string strict_why;
    if (!face.font().strict_check(&strict_why))
        return refuse(face.name() + " cannot be given to a rasteriser: " + strict_why);

    std::vector<uint8_t> bytes;
    if (!build_atlas_font(face, ATLAS_WCHAR_BITS, bytes, why))
        return nullptr;
    // ImGui takes anything this short for a mistaken size argument.
    if (bytes.size() <= 100)
        return refuse("the font is too small to be a font");

    ImFontConfig cfg = config != nullptr ? *config : ImFontConfig();
    cfg.FontData = nullptr;
    cfg.FontDataSize = 0;
    cfg.FontDataOwnedByAtlas = true;
    if (cfg.OversampleH == 0)
        cfg.OversampleH = 1;
    if (cfg.Name[0] == '\0')
        std::snprintf(cfg.Name, sizeof cfg.Name, "%s", face.name().c_str());

    // The atlas takes the allocation and frees it with IM_FREE, on failure too.
    void* owned = IM_ALLOC(bytes.size());
    std::memcpy(owned, bytes.data(), bytes.size());
    ImFont* font =
        atlas->AddFontFromMemoryTTF(owned, static_cast<int>(bytes.size()), size_pixels, &cfg);
    if (font == nullptr && why != nullptr)
        *why = "ImGui could not load " + face.name();
    return font;
}

} // namespace ImTypeset::detail
