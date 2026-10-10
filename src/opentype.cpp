#include "opentype.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace ImTypeset::detail
{

namespace
{

// --- Bounds-checked big-endian reads --------------------------------------------

// A window on the font's bytes. A read outside the window answers zero and
// sets `*bad`, which every window cut from the same parse shares, so a parser
// reads a whole structure and tests once.
struct Reader {
    const uint8_t* p = nullptr;
    size_t n = 0;
    bool* bad = nullptr;

    bool has(size_t offset, size_t length) const { return offset <= n && length <= n - offset; }

    bool need(size_t offset, size_t length) const
    {
        if (has(offset, length))
            return true;
        *bad = true;
        return false;
    }

    uint8_t u8(size_t offset) const { return need(offset, 1) ? p[offset] : uint8_t(0); }

    uint16_t u16(size_t offset) const
    {
        if (!need(offset, 2))
            return 0;
        return static_cast<uint16_t>((p[offset] << 8) | p[offset + 1]);
    }

    int16_t s16(size_t offset) const { return static_cast<int16_t>(u16(offset)); }

    uint32_t u32(size_t offset) const
    {
        if (!need(offset, 4))
            return 0;
        return (uint32_t(p[offset]) << 24) | (uint32_t(p[offset + 1]) << 16) |
               (uint32_t(p[offset + 2]) << 8) | uint32_t(p[offset + 3]);
    }

    // The window from `offset` to the end. Past the end: empty, and bad.
    Reader from(size_t offset) const
    {
        if (!need(offset, 0))
            return Reader{nullptr, 0, bad};
        return Reader{p + offset, n - offset, bad};
    }
};

constexpr uint32_t tag_of(const char (&t)[5])
{
    return (uint32_t(static_cast<uint8_t>(t[0])) << 24) |
           (uint32_t(static_cast<uint8_t>(t[1])) << 16) |
           (uint32_t(static_cast<uint8_t>(t[2])) << 8) | uint32_t(static_cast<uint8_t>(t[3]));
}

int popcount16(unsigned v)
{
    int count = 0;
    for (; v != 0; v &= v - 1)
        ++count;
    return count;
}

// --- Limits on what a file may make us do ------------------------------------------

// Nested composite glyphs. Real fonts nest two or three deep.
constexpr int MAX_COMPONENT_DEPTH = 32;
// Work spent drawing one glyph, across all nesting: a point costs 1 and a
// component 16. A real glyph has a few hundred points.
constexpr int GLYF_WORK_BUDGET = 1 << 20;
constexpr int GLYF_COMPONENT_COST = 16;
// The Type 2 specification's limits are 48 operands and 10 nested calls.
constexpr int CFF_STACK_LIMIT = 96;
constexpr int CFF_CALL_DEPTH = 10;
// Work spent on one glyph, subroutines included: an operator costs 1 and so
// does each segment it draws.
constexpr int CFF_WORK_BUDGET = 1 << 20;
// A coverage table cannot name more glyphs than a font can have.
constexpr size_t MAX_COVERAGE = 65536;
// Tables whose records may overlap one another can be made to describe far
// more than their bytes could hold honestly. These bound what is read: glyphs
// classed by one ClassDef, codepoints assigned by one format 4 cmap, lookups
// named by 'kern' features, and pair subtables kept.
constexpr size_t MAX_CLASSDEF_WORK = 4 * 65536;
constexpr size_t MAX_CMAP4_WORK = 8 * 65536;
constexpr size_t MAX_KERN_LOOKUPS = 4096;
constexpr size_t MAX_KERN_SUBTABLES = 1024;
// Pair subtables may also share their bytes with one another, so what all of
// them together may make this reader keep is bounded by the table's size: one
// unit per pair or class-matrix cell stored. Tables that share nothing need at
// least two bytes per unit; the factor leaves room for fonts whose compiler
// shared identical pair sets between first glyphs.
constexpr size_t KERN_UNITS_PER_BYTE = 4;
constexpr size_t KERN_UNITS_FLOOR = 4096;
// A class subtable is kept as two classes and a flag per glyph id; this bounds
// the bytes all of them take.
constexpr size_t KERN_CLASS_BYTES = size_t(1) << 25;
// FDSelect names a Font DICT in one byte.
constexpr size_t CFF_MAX_FONT_DICTS = 256;
// The transient array of the Type 2 storage operators.
constexpr int CFF_TRANSIENT_SIZE = 32;
// No glyph has a Font DICT.
constexpr uint16_t CFF_NO_FONT_DICT = 0xFFFF;

// What the strict pass allows a glyph, beyond being well formed. Nesting and
// the size of a composite once every component is expanded are bounded
// because stb_truetype recurses and allocates for the whole expansion with no
// limit of its own; the limits are far above what fonts contain (the format
// cannot state more than 65,535 points for a glyph).
constexpr int STRICT_COMPONENT_DEPTH = 16;
constexpr uint32_t STRICT_COMPOSITE_POINTS = 65535;
constexpr uint32_t STRICT_COMPOSITE_COMPONENTS = 256;
constexpr int STRICT_CFF_WORK = 1 << 16;
// A glyph may reach this many times the font's ascender-to-descender height
// from its origin. Rasterisers scale by that height, so the bitmap a glyph
// asks for is at most twice this many times the pixel size across. Of 2,905
// installed fonts measured, the furthest-reaching glyph is between 6 and 8
// heights out (the Arabic ligatures of Amiri).
constexpr double STRICT_REACH = 16.0;

// --- Layout common tables --------------------------------------------------------

// The glyphs of a Coverage table in coverage-index order. Format 2 ranges are
// taken in order of StartCoverageIndex, as fontTools takes them.
bool read_coverage(const Reader& r, std::vector<uint16_t>& out)
{
    out.clear();
    const unsigned format = r.u16(0);
    const size_t count = r.u16(2);
    if (*r.bad)
        return false;
    if (format == 1) {
        if (!r.need(4, count * 2))
            return false;
        out.reserve(count);
        for (size_t i = 0; i < count; ++i)
            out.push_back(r.u16(4 + i * 2));
        return true;
    }
    if (format != 2 || !r.need(4, count * 6))
        return false;
    struct Range {
        uint16_t start, end, index;
    };
    std::vector<Range> ranges;
    ranges.reserve(count);
    for (size_t i = 0; i < count; ++i)
        ranges.push_back({r.u16(4 + i * 6), r.u16(6 + i * 6), r.u16(8 + i * 6)});
    std::stable_sort(ranges.begin(), ranges.end(),
                     [](const Range& a, const Range& b) { return a.index < b.index; });
    for (const Range& range : ranges) {
        for (unsigned g = range.start; g <= range.end; ++g) {
            if (out.size() >= MAX_COVERAGE)
                return false;
            out.push_back(static_cast<uint16_t>(g));
        }
    }
    return true;
}

// A ClassDef table as a class per glyph id, zero where the table is silent.
bool read_classdef(const Reader& r, uint32_t glyph_count, std::vector<uint16_t>& out)
{
    out.assign(glyph_count, 0);
    const unsigned format = r.u16(0);
    if (*r.bad)
        return false;
    if (format == 1) {
        const uint32_t start = r.u16(2);
        const size_t count = r.u16(4);
        if (!r.need(6, count * 2))
            return false;
        for (size_t i = 0; i < count; ++i) {
            const uint32_t glyph = start + static_cast<uint32_t>(i);
            if (glyph < glyph_count)
                out[glyph] = r.u16(6 + i * 2);
        }
        return true;
    }
    if (format != 2)
        return false;
    const size_t count = r.u16(2);
    if (!r.need(4, count * 6))
        return false;
    size_t work = 0;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t start = r.u16(4 + i * 6);
        const uint32_t end = r.u16(6 + i * 6);
        const uint16_t cls = r.u16(8 + i * 6);
        // A range of class 0 says what silence says, and does not take back
        // a class an earlier range gave.
        if (cls == 0)
            continue;
        for (uint32_t g = start; g <= end && g < glyph_count; ++g) {
            if (++work > MAX_CLASSDEF_WORK)
                return false;
            out[g] = cls;
        }
    }
    return true;
}

// --- CFF -------------------------------------------------------------------------

// An INDEX at `offset`: the absolute offsets of its count + 1 object
// boundaries, and where the INDEX ends. An empty INDEX has no offsets.
bool read_index(const Reader& cff, size_t offset, std::vector<uint32_t>& offsets, size_t& end)
{
    offsets.clear();
    const size_t count = cff.u16(offset);
    if (*cff.bad)
        return false;
    if (count == 0) {
        end = offset + 2;
        return true;
    }
    const size_t off_size = cff.u8(offset + 2);
    if (off_size < 1 || off_size > 4 || !cff.need(offset + 3, (count + 1) * off_size))
        return false;
    const size_t base = offset + 3 + (count + 1) * off_size - 1;
    offsets.reserve(count + 1);
    size_t previous = 0;
    for (size_t i = 0; i <= count; ++i) {
        size_t value = 0;
        for (size_t b = 0; b < off_size; ++b)
            value = (value << 8) | cff.p[offset + 3 + i * off_size + b];
        if (value < 1 || value < previous || !cff.has(base, value))
            return false;
        previous = value;
        offsets.push_back(static_cast<uint32_t>(base + value));
    }
    end = offsets.back();
    return true;
}

// The operands a DICT gives the operators this reader needs. Operators it does
// not name are skipped.
struct CffDict {
    bool has_charstrings = false, has_private = false, has_charset = false, has_subrs = false;
    bool is_cid = false;
    bool has_fd_array = false, has_fd_select = false;
    double charstrings = 0, private_size = 0, private_offset = 0, charset = 0, subrs = 0;
    double fd_array = 0, fd_select = 0;
    double charstring_type = 2;
    // False when two readers of this DICT could take different values from
    // it: one of the operators above appears twice, or with more or fewer
    // operands than it takes, or with a real number for an operand. This
    // reader takes the last occurrence and the last operands; stb_truetype
    // takes the first of each.
    bool unambiguous = true;
    // The font has per-glyph Font DICTs (FDArray or FDSelect).
    bool has_fd = false;
};

bool read_dict(const Reader& d, CffDict& out)
{
    std::array<double, 48> stack{};
    size_t depth = 0;
    size_t i = 0;
    bool real_operand = false;
    int seen[7] = {0, 0, 0, 0, 0, 0, 0};
    // One of the operators read: `slot` counts its occurrences.
    const auto read = [&](int slot, size_t operands) {
        if (++seen[slot] > 1 || depth != operands || real_operand)
            out.unambiguous = false;
    };
    while (i < d.n) {
        const unsigned b = d.p[i];
        if (b <= 21) {
            unsigned op = b;
            ++i;
            if (b == 12) {
                if (i >= d.n)
                    return false;
                op = 1200 + d.p[i];
                ++i;
            }
            const double last = depth > 0 ? stack[depth - 1] : 0.0;
            switch (op) {
            case 15:
                read(0, 1);
                out.has_charset = depth > 0;
                out.charset = last;
                break;
            case 17:
                read(1, 1);
                out.has_charstrings = depth > 0;
                out.charstrings = last;
                break;
            case 18:
                read(2, 2);
                if (depth >= 2) {
                    out.has_private = true;
                    out.private_size = stack[depth - 2];
                    out.private_offset = last;
                }
                break;
            case 19:
                read(3, 1);
                out.has_subrs = depth > 0;
                out.subrs = last;
                break;
            case 1206:
                read(4, 1);
                if (depth > 0)
                    out.charstring_type = last;
                break;
            case 1230:
                out.is_cid = true;
                break;
            case 1236:
                read(5, 1);
                out.has_fd = true;
                out.has_fd_array = depth > 0;
                out.fd_array = last;
                break;
            case 1237:
                read(6, 1);
                out.has_fd = true;
                out.has_fd_select = depth > 0;
                out.fd_select = last;
                break;
            default:
                break;
            }
            depth = 0;
            real_operand = false;
            continue;
        }
        double value = 0;
        if (b == 28) {
            if (!d.has(i, 3))
                return false;
            value = static_cast<int16_t>((d.p[i + 1] << 8) | d.p[i + 2]);
            i += 3;
        } else if (b == 29) {
            if (!d.has(i, 5))
                return false;
            value =
                static_cast<int32_t>((uint32_t(d.p[i + 1]) << 24) | (uint32_t(d.p[i + 2]) << 16) |
                                     (uint32_t(d.p[i + 3]) << 8) | uint32_t(d.p[i + 4]));
            i += 5;
        } else if (b == 30) {
            // A real number: nothing read here is one, so only its length matters.
            real_operand = true;
            ++i;
            for (;;) {
                if (i >= d.n)
                    return false;
                const unsigned byte = d.p[i++];
                if ((byte & 0x0F) == 0x0F || (byte >> 4) == 0x0F)
                    break;
            }
        } else if (b >= 32 && b <= 246) {
            value = static_cast<int>(b) - 139;
            ++i;
        } else if (b >= 247 && b <= 254) {
            if (!d.has(i, 2))
                return false;
            const int w = d.p[i + 1];
            value = b <= 250 ? (static_cast<int>(b) - 247) * 256 + w + 108
                             : -(static_cast<int>(b) - 251) * 256 - w - 108;
            i += 2;
        } else {
            return false; // 22..27, 31 and 255 are reserved in a DICT
        }
        if (depth == stack.size())
            return false;
        stack[depth++] = value;
    }
    return true;
}

// True if `value`, a DICT operand, is an offset inside a table of `size` bytes.
bool as_offset(double value, size_t size, size_t& out)
{
    if (!(value >= 0.0) || value > static_cast<double>(size))
        return false;
    out = static_cast<size_t>(value);
    return true;
}

int subr_bias(size_t count)
{
    return count < 1240 ? 107 : count < 33900 ? 1131 : 32768;
}

// The 391 standard strings of the CFF specification (Appendix A): string id to
// name. A glyph whose charset entry is below 391 is named from here.
constexpr size_t CFF_STANDARD_STRING_COUNT = 391;
const std::string_view CFF_STANDARD_STRINGS[CFF_STANDARD_STRING_COUNT] = {
    ".notdef",
    "space",
    "exclam",
    "quotedbl",
    "numbersign",
    "dollar",
    "percent",
    "ampersand",
    "quoteright",
    "parenleft",
    "parenright",
    "asterisk",
    "plus",
    "comma",
    "hyphen",
    "period",
    "slash",
    "zero",
    "one",
    "two",
    "three",
    "four",
    "five",
    "six",
    "seven",
    "eight",
    "nine",
    "colon",
    "semicolon",
    "less",
    "equal",
    "greater",
    "question",
    "at",
    "A",
    "B",
    "C",
    "D",
    "E",
    "F",
    "G",
    "H",
    "I",
    "J",
    "K",
    "L",
    "M",
    "N",
    "O",
    "P",
    "Q",
    "R",
    "S",
    "T",
    "U",
    "V",
    "W",
    "X",
    "Y",
    "Z",
    "bracketleft",
    "backslash",
    "bracketright",
    "asciicircum",
    "underscore",
    "quoteleft",
    "a",
    "b",
    "c",
    "d",
    "e",
    "f",
    "g",
    "h",
    "i",
    "j",
    "k",
    "l",
    "m",
    "n",
    "o",
    "p",
    "q",
    "r",
    "s",
    "t",
    "u",
    "v",
    "w",
    "x",
    "y",
    "z",
    "braceleft",
    "bar",
    "braceright",
    "asciitilde",
    "exclamdown",
    "cent",
    "sterling",
    "fraction",
    "yen",
    "florin",
    "section",
    "currency",
    "quotesingle",
    "quotedblleft",
    "guillemotleft",
    "guilsinglleft",
    "guilsinglright",
    "fi",
    "fl",
    "endash",
    "dagger",
    "daggerdbl",
    "periodcentered",
    "paragraph",
    "bullet",
    "quotesinglbase",
    "quotedblbase",
    "quotedblright",
    "guillemotright",
    "ellipsis",
    "perthousand",
    "questiondown",
    "grave",
    "acute",
    "circumflex",
    "tilde",
    "macron",
    "breve",
    "dotaccent",
    "dieresis",
    "ring",
    "cedilla",
    "hungarumlaut",
    "ogonek",
    "caron",
    "emdash",
    "AE",
    "ordfeminine",
    "Lslash",
    "Oslash",
    "OE",
    "ordmasculine",
    "ae",
    "dotlessi",
    "lslash",
    "oslash",
    "oe",
    "germandbls",
    "onesuperior",
    "logicalnot",
    "mu",
    "trademark",
    "Eth",
    "onehalf",
    "plusminus",
    "Thorn",
    "onequarter",
    "divide",
    "brokenbar",
    "degree",
    "thorn",
    "threequarters",
    "twosuperior",
    "registered",
    "minus",
    "eth",
    "multiply",
    "threesuperior",
    "copyright",
    "Aacute",
    "Acircumflex",
    "Adieresis",
    "Agrave",
    "Aring",
    "Atilde",
    "Ccedilla",
    "Eacute",
    "Ecircumflex",
    "Edieresis",
    "Egrave",
    "Iacute",
    "Icircumflex",
    "Idieresis",
    "Igrave",
    "Ntilde",
    "Oacute",
    "Ocircumflex",
    "Odieresis",
    "Ograve",
    "Otilde",
    "Scaron",
    "Uacute",
    "Ucircumflex",
    "Udieresis",
    "Ugrave",
    "Yacute",
    "Ydieresis",
    "Zcaron",
    "aacute",
    "acircumflex",
    "adieresis",
    "agrave",
    "aring",
    "atilde",
    "ccedilla",
    "eacute",
    "ecircumflex",
    "edieresis",
    "egrave",
    "iacute",
    "icircumflex",
    "idieresis",
    "igrave",
    "ntilde",
    "oacute",
    "ocircumflex",
    "odieresis",
    "ograve",
    "otilde",
    "scaron",
    "uacute",
    "ucircumflex",
    "udieresis",
    "ugrave",
    "yacute",
    "ydieresis",
    "zcaron",
    "exclamsmall",
    "Hungarumlautsmall",
    "dollaroldstyle",
    "dollarsuperior",
    "ampersandsmall",
    "Acutesmall",
    "parenleftsuperior",
    "parenrightsuperior",
    "twodotenleader",
    "onedotenleader",
    "zerooldstyle",
    "oneoldstyle",
    "twooldstyle",
    "threeoldstyle",
    "fouroldstyle",
    "fiveoldstyle",
    "sixoldstyle",
    "sevenoldstyle",
    "eightoldstyle",
    "nineoldstyle",
    "commasuperior",
    "threequartersemdash",
    "periodsuperior",
    "questionsmall",
    "asuperior",
    "bsuperior",
    "centsuperior",
    "dsuperior",
    "esuperior",
    "isuperior",
    "lsuperior",
    "msuperior",
    "nsuperior",
    "osuperior",
    "rsuperior",
    "ssuperior",
    "tsuperior",
    "ff",
    "ffi",
    "ffl",
    "parenleftinferior",
    "parenrightinferior",
    "Circumflexsmall",
    "hyphensuperior",
    "Gravesmall",
    "Asmall",
    "Bsmall",
    "Csmall",
    "Dsmall",
    "Esmall",
    "Fsmall",
    "Gsmall",
    "Hsmall",
    "Ismall",
    "Jsmall",
    "Ksmall",
    "Lsmall",
    "Msmall",
    "Nsmall",
    "Osmall",
    "Psmall",
    "Qsmall",
    "Rsmall",
    "Ssmall",
    "Tsmall",
    "Usmall",
    "Vsmall",
    "Wsmall",
    "Xsmall",
    "Ysmall",
    "Zsmall",
    "colonmonetary",
    "onefitted",
    "rupiah",
    "Tildesmall",
    "exclamdownsmall",
    "centoldstyle",
    "Lslashsmall",
    "Scaronsmall",
    "Zcaronsmall",
    "Dieresissmall",
    "Brevesmall",
    "Caronsmall",
    "Dotaccentsmall",
    "Macronsmall",
    "figuredash",
    "hypheninferior",
    "Ogoneksmall",
    "Ringsmall",
    "Cedillasmall",
    "questiondownsmall",
    "oneeighth",
    "threeeighths",
    "fiveeighths",
    "seveneighths",
    "onethird",
    "twothirds",
    "zerosuperior",
    "foursuperior",
    "fivesuperior",
    "sixsuperior",
    "sevensuperior",
    "eightsuperior",
    "ninesuperior",
    "zeroinferior",
    "oneinferior",
    "twoinferior",
    "threeinferior",
    "fourinferior",
    "fiveinferior",
    "sixinferior",
    "seveninferior",
    "eightinferior",
    "nineinferior",
    "centinferior",
    "dollarinferior",
    "periodinferior",
    "commainferior",
    "Agravesmall",
    "Aacutesmall",
    "Acircumflexsmall",
    "Atildesmall",
    "Adieresissmall",
    "Aringsmall",
    "AEsmall",
    "Ccedillasmall",
    "Egravesmall",
    "Eacutesmall",
    "Ecircumflexsmall",
    "Edieresissmall",
    "Igravesmall",
    "Iacutesmall",
    "Icircumflexsmall",
    "Idieresissmall",
    "Ethsmall",
    "Ntildesmall",
    "Ogravesmall",
    "Oacutesmall",
    "Ocircumflexsmall",
    "Otildesmall",
    "Odieresissmall",
    "OEsmall",
    "Oslashsmall",
    "Ugravesmall",
    "Uacutesmall",
    "Ucircumflexsmall",
    "Udieresissmall",
    "Yacutesmall",
    "Thornsmall",
    "Ydieresissmall",
    "001.000",
    "001.001",
    "001.002",
    "001.003",
    "Black",
    "Bold",
    "Book",
    "Light",
    "Medium",
    "Regular",
    "Roman",
    "Semibold",
};

// The 258 glyph names of the standard Macintosh ordering, which a format 1
// post table means and a format 2 one refers to by number (OpenType, 'post').
constexpr size_t MAC_GLYPH_NAME_COUNT = 258;
const std::string_view MAC_GLYPH_NAMES[MAC_GLYPH_NAME_COUNT] = {
    ".notdef",
    ".null",
    "nonmarkingreturn",
    "space",
    "exclam",
    "quotedbl",
    "numbersign",
    "dollar",
    "percent",
    "ampersand",
    "quotesingle",
    "parenleft",
    "parenright",
    "asterisk",
    "plus",
    "comma",
    "hyphen",
    "period",
    "slash",
    "zero",
    "one",
    "two",
    "three",
    "four",
    "five",
    "six",
    "seven",
    "eight",
    "nine",
    "colon",
    "semicolon",
    "less",
    "equal",
    "greater",
    "question",
    "at",
    "A",
    "B",
    "C",
    "D",
    "E",
    "F",
    "G",
    "H",
    "I",
    "J",
    "K",
    "L",
    "M",
    "N",
    "O",
    "P",
    "Q",
    "R",
    "S",
    "T",
    "U",
    "V",
    "W",
    "X",
    "Y",
    "Z",
    "bracketleft",
    "backslash",
    "bracketright",
    "asciicircum",
    "underscore",
    "grave",
    "a",
    "b",
    "c",
    "d",
    "e",
    "f",
    "g",
    "h",
    "i",
    "j",
    "k",
    "l",
    "m",
    "n",
    "o",
    "p",
    "q",
    "r",
    "s",
    "t",
    "u",
    "v",
    "w",
    "x",
    "y",
    "z",
    "braceleft",
    "bar",
    "braceright",
    "asciitilde",
    "Adieresis",
    "Aring",
    "Ccedilla",
    "Eacute",
    "Ntilde",
    "Odieresis",
    "Udieresis",
    "aacute",
    "agrave",
    "acircumflex",
    "adieresis",
    "atilde",
    "aring",
    "ccedilla",
    "eacute",
    "egrave",
    "ecircumflex",
    "edieresis",
    "iacute",
    "igrave",
    "icircumflex",
    "idieresis",
    "ntilde",
    "oacute",
    "ograve",
    "ocircumflex",
    "odieresis",
    "otilde",
    "uacute",
    "ugrave",
    "ucircumflex",
    "udieresis",
    "dagger",
    "degree",
    "cent",
    "sterling",
    "section",
    "bullet",
    "paragraph",
    "germandbls",
    "registered",
    "copyright",
    "trademark",
    "acute",
    "dieresis",
    "notequal",
    "AE",
    "Oslash",
    "infinity",
    "plusminus",
    "lessequal",
    "greaterequal",
    "yen",
    "mu",
    "partialdiff",
    "summation",
    "product",
    "pi",
    "integral",
    "ordfeminine",
    "ordmasculine",
    "Omega",
    "ae",
    "oslash",
    "questiondown",
    "exclamdown",
    "logicalnot",
    "radical",
    "florin",
    "approxequal",
    "Delta",
    "guillemotleft",
    "guillemotright",
    "ellipsis",
    "nonbreakingspace",
    "Agrave",
    "Atilde",
    "Otilde",
    "OE",
    "oe",
    "endash",
    "emdash",
    "quotedblleft",
    "quotedblright",
    "quoteleft",
    "quoteright",
    "divide",
    "lozenge",
    "ydieresis",
    "Ydieresis",
    "fraction",
    "currency",
    "guilsinglleft",
    "guilsinglright",
    "fi",
    "fl",
    "daggerdbl",
    "periodcentered",
    "quotesinglbase",
    "quotedblbase",
    "perthousand",
    "Acircumflex",
    "Ecircumflex",
    "Aacute",
    "Edieresis",
    "Egrave",
    "Iacute",
    "Icircumflex",
    "Idieresis",
    "Igrave",
    "Oacute",
    "Ocircumflex",
    "apple",
    "Ograve",
    "Uacute",
    "Ucircumflex",
    "Ugrave",
    "dotlessi",
    "circumflex",
    "tilde",
    "macron",
    "breve",
    "dotaccent",
    "ring",
    "cedilla",
    "hungarumlaut",
    "ogonek",
    "caron",
    "Lslash",
    "lslash",
    "Scaron",
    "scaron",
    "Zcaron",
    "zcaron",
    "brokenbar",
    "Eth",
    "eth",
    "Yacute",
    "yacute",
    "Thorn",
    "thorn",
    "minus",
    "multiply",
    "onesuperior",
    "twosuperior",
    "threesuperior",
    "onehalf",
    "onequarter",
    "threequarters",
    "franc",
    "Gbreve",
    "gbreve",
    "Idotaccent",
    "Scedilla",
    "scedilla",
    "Cacute",
    "cacute",
    "Ccaron",
    "ccaron",
    "dcroat",
};

// The predefined Expert charset of the CFF specification (Appendix C): the
// string id of each glyph, glyph 0 first.
const uint16_t CFF_EXPERT_CHARSET[] = {
    0,   1,   229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 13,  14,  15,  99,  239, 240, 241,
    242, 243, 244, 245, 246, 247, 248, 27,  28,  249, 250, 251, 252, 253, 254, 255, 256, 257, 258,
    259, 260, 261, 262, 263, 264, 265, 266, 109, 110, 267, 268, 269, 270, 271, 272, 273, 274, 275,
    276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294,
    295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313,
    314, 315, 316, 317, 318, 158, 155, 163, 319, 320, 321, 322, 323, 324, 325, 326, 150, 164, 169,
    327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345,
    346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358, 359, 360, 361, 362, 363, 364,
    365, 366, 367, 368, 369, 370, 371, 372, 373, 374, 375, 376, 377, 378,
};

// The predefined Expert Subset charset (Appendix C).
const uint16_t CFF_EXPERT_SUBSET_CHARSET[] = {
    0,   1,   231, 232, 235, 236, 237, 238, 13,  14,  15,  99,  239, 240, 241, 242, 243, 244,
    245, 246, 247, 248, 27,  28,  249, 250, 251, 253, 254, 255, 256, 257, 258, 259, 260, 261,
    262, 263, 264, 265, 266, 109, 110, 267, 268, 269, 270, 272, 300, 301, 302, 305, 314, 315,
    158, 155, 163, 320, 321, 322, 323, 324, 325, 326, 150, 164, 169, 327, 328, 329, 330, 331,
    332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346,
};

// Macintosh Roman character codes 0x80..0xFF as Unicode; below 0x80 the two agree.
const uint16_t MAC_ROMAN_HIGH[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3,
    0x00E5, 0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3,
    0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA,
    0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D,
    0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE,
    0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

// --- MathConstants ---------------------------------------------------------------

const char* const MATH_CONSTANT_NAMES[MATH_CONSTANT_COUNT] = {
    "ScriptPercentScaleDown",
    "ScriptScriptPercentScaleDown",
    "DelimitedSubFormulaMinHeight",
    "DisplayOperatorMinHeight",
    "MathLeading",
    "AxisHeight",
    "AccentBaseHeight",
    "FlattenedAccentBaseHeight",
    "SubscriptShiftDown",
    "SubscriptTopMax",
    "SubscriptBaselineDropMin",
    "SuperscriptShiftUp",
    "SuperscriptShiftUpCramped",
    "SuperscriptBottomMin",
    "SuperscriptBaselineDropMax",
    "SubSuperscriptGapMin",
    "SuperscriptBottomMaxWithSubscript",
    "SpaceAfterScript",
    "UpperLimitGapMin",
    "UpperLimitBaselineRiseMin",
    "LowerLimitGapMin",
    "LowerLimitBaselineDropMin",
    "StackTopShiftUp",
    "StackTopDisplayStyleShiftUp",
    "StackBottomShiftDown",
    "StackBottomDisplayStyleShiftDown",
    "StackGapMin",
    "StackDisplayStyleGapMin",
    "StretchStackTopShiftUp",
    "StretchStackBottomShiftDown",
    "StretchStackGapAboveMin",
    "StretchStackGapBelowMin",
    "FractionNumeratorShiftUp",
    "FractionNumeratorDisplayStyleShiftUp",
    "FractionDenominatorShiftDown",
    "FractionDenominatorDisplayStyleShiftDown",
    "FractionNumeratorGapMin",
    "FractionNumDisplayStyleGapMin",
    "FractionRuleThickness",
    "FractionDenominatorGapMin",
    "FractionDenomDisplayStyleGapMin",
    "SkewedFractionHorizontalGap",
    "SkewedFractionVerticalGap",
    "OverbarVerticalGap",
    "OverbarRuleThickness",
    "OverbarExtraAscender",
    "UnderbarVerticalGap",
    "UnderbarRuleThickness",
    "UnderbarExtraDescender",
    "RadicalVerticalGap",
    "RadicalDisplayStyleVerticalGap",
    "RadicalRuleThickness",
    "RadicalExtraAscender",
    "RadicalKernBeforeDegree",
    "RadicalKernAfterDegree",
    "RadicalDegreeBottomRaisePercent",
};

} // namespace

const char* math_constant_name(int index)
{
    return index >= 0 && index < MATH_CONSTANT_COUNT ? MATH_CONSTANT_NAMES[index] : "";
}

// --- The pen -----------------------------------------------------------------------

// clang-format off
// fontTools' BoundsPen, operation for operation: the doubles it produces are
// the reference's, so the order of every expression here is fontTools' and is
// not to be rearranged. tools/oracle/FORMAT-font.md states the procedure and
// tools/oracle/check_font_bounds.py is the same thing in Python.
//
// UPSTREAM NOTICE. These routines in this file follow fontTools 4.66.0
// expression for expression, because bit-equal results need the same
// operations in the same order:
//   - BoundsPen::solve_quadratic: solveQuadratic, misc/bezierTools.py;
//   - BoundsPen::curve_to: calcCubicParameters and calcCubicBounds,
//     misc/bezierTools.py;
//   - BoundsPen::quad_to: calcQuadraticBounds, misc/bezierTools.py;
//   - the hflex1 and flex1 operators of the Type 2 charstring interpreter:
//     op_hflex1 and op_flex1, misc/psCharStrings.py;
//   - the cmap format 4 decode: cmap_format_4.decompile,
//     ttLib/tables/_c_m_a_p.py.
// Two more follow fontTools' rules, not its expressions, because what they
// decide is which codepoints and names the reference sees:
//   - which groups of a cmap format 12 or 13 subtable are taken (cut off at
//     U+10FFFF, left out when out of order, glyph 0 not mapped):
//     cmap_format_12_or_13.decompile and _make_map, ttLib/tables/_c_m_a_p.py;
//   - the name given to a glyph a format 2 post table leaves unnamed or
//     names twice: decode_format_2_0, unpackPStrings and build_psNameMapping,
//     ttLib/tables/_p_o_s_t.py.
// The rest of this reader is written from the OpenType specification.
// fontTools is distributed under the following licence (THIRD-PARTY.md):
//
// MIT License
//
// Copyright (c) 2017 Just van Rossum
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// clang-format on
struct OpenTypeFont::BoundsPen {
    bool any = false;
    double x_min = 0, y_min = 0, x_max = 0, y_max = 0;
    double cur_x = 0, cur_y = 0;

    void add(double x, double y)
    {
        if (!any) {
            any = true;
            x_min = x_max = x;
            y_min = y_max = y;
            return;
        }
        x_min = std::min(x_min, x);
        y_min = std::min(y_min, y);
        x_max = std::max(x_max, x);
        y_max = std::max(y_max, y);
    }

    bool inside(double x, double y) const
    {
        return x_min <= x && x <= x_max && y_min <= y && y <= y_max;
    }

    void move_to(double x, double y)
    {
        add(x, y);
        cur_x = x;
        cur_y = y;
    }

    void line_to(double x, double y)
    {
        add(x, y);
        cur_x = x;
        cur_y = y;
    }

    // One quadratic segment from the current point: control (cx, cy), end (x, y).
    void quad_to(double cx, double cy, double x, double y)
    {
        add(x, y);
        if (!inside(cx, cy)) {
            const double bx = (cx - cur_x) * 2.0;
            const double by = (cy - cur_y) * 2.0;
            const double ax = x - cur_x - bx;
            const double ay = y - cur_y - by;
            const double ax2 = ax * 2.0;
            const double ay2 = ay * 2.0;
            double roots[2];
            int count = 0;
            if (ax2 != 0)
                roots[count++] = -bx / ax2;
            if (ay2 != 0)
                roots[count++] = -by / ay2;
            const double sx = cur_x, sy = cur_y;
            for (int i = 0; i < count; ++i) {
                const double t = roots[i];
                if (0 <= t && t < 1)
                    add(ax * t * t + bx * t + sx, ay * t * t + by * t + sy);
            }
        }
        cur_x = x;
        cur_y = y;
    }

    static int solve_quadratic(double a, double b, double c, double* roots)
    {
        if (std::fabs(a) < 1e-10) {
            if (std::fabs(b) < 1e-10)
                return 0;
            roots[0] = -c / b;
            return 1;
        }
        const double dd = b * b - 4.0 * a * c;
        if (dd >= 0.0) {
            const double r = std::sqrt(dd);
            roots[0] = (-b + r) / 2.0 / a;
            roots[1] = (-b - r) / 2.0 / a;
            return 2;
        }
        return 0;
    }

    void curve_to(double c1x, double c1y, double c2x, double c2y, double x, double y)
    {
        add(x, y);
        if (!inside(c1x, c1y) || !inside(c2x, c2y)) {
            const double dx = cur_x, dy = cur_y;
            const double cx = (c1x - dx) * 3.0;
            const double cy = (c1y - dy) * 3.0;
            const double bx = (c2x - c1x) * 3.0 - cx;
            const double by = (c2y - c1y) * 3.0 - cy;
            const double ax = x - dx - cx - bx;
            const double ay = y - dy - cy - by;
            double roots[4];
            int count = 0;
            double found[2];
            int n = solve_quadratic(ax * 3.0, bx * 2.0, cx, found);
            for (int i = 0; i < n; ++i)
                if (0 <= found[i] && found[i] < 1)
                    roots[count++] = found[i];
            n = solve_quadratic(ay * 3.0, by * 2.0, cy, found);
            for (int i = 0; i < n; ++i)
                if (0 <= found[i] && found[i] < 1)
                    roots[count++] = found[i];
            for (int i = 0; i < count; ++i) {
                const double t = roots[i];
                add(ax * t * t * t + bx * t * t + cx * t + dx,
                    ay * t * t * t + by * t * t + cy * t + dy);
            }
        }
        cur_x = x;
        cur_y = y;
    }
};

// --- Loading -------------------------------------------------------------------------

void OpenTypeFont::reset()
{
    *this = OpenTypeFont();
}

bool OpenTypeFont::fail(std::string* why, const std::string& reason)
{
    // `reason` may be a view of this object's own state.
    const std::string kept = reason;
    reset();
    if (why != nullptr)
        *why = kept;
    return false;
}

void OpenTypeFont::warn(const char* what) const
{
    // A font with thousands of broken glyphs says so once per kind, not per glyph.
    for (const std::string& seen : _warnings)
        if (seen == what)
            return;
    if (_warnings.size() < 32)
        _warnings.emplace_back(what);
}

OpenTypeFont::Table OpenTypeFont::table(const char* tag) const
{
    const uint32_t wanted = (uint32_t(static_cast<uint8_t>(tag[0])) << 24) |
                            (uint32_t(static_cast<uint8_t>(tag[1])) << 16) |
                            (uint32_t(static_cast<uint8_t>(tag[2])) << 8) |
                            uint32_t(static_cast<uint8_t>(tag[3]));
    for (const DirectoryEntry& entry : _directory)
        if (entry.tag == wanted)
            return Table{_data + entry.offset, entry.length};
    return Table{};
}

TableRecord OpenTypeFont::table_record(size_t index) const
{
    if (index >= _directory.size())
        return TableRecord{};
    const DirectoryEntry& entry = _directory[index];
    return TableRecord{entry.tag, _data + entry.offset, entry.length};
}

uint32_t OpenTypeFont::faces_in(const uint8_t* data, size_t size)
{
    if (data == nullptr || size < 4)
        return 0;
    bool bad = false;
    const Reader file{data, size, &bad};
    if (file.u32(0) != tag_of("ttcf"))
        return 1;
    const uint32_t count = file.u32(8);
    return bad ? 0 : count;
}

bool OpenTypeFont::load(const uint8_t* data, size_t size, std::string* why,
                        const FontLoadOptions& options)
{
    reset();
    if (data == nullptr || size == 0)
        return fail(why, "the font is empty");
    _data = data;
    _size = size;
    if (!read_directory(why, options.face_index) || !read_head_maxp_hhea(why))
        return false;
    read_os2();
    if (!read_hmtx(why) || !read_cmap(why) || !read_outline_tables(why))
        return false;
    read_gpos();
    if (options.legacy_kern)
        read_legacy_kern();
    read_math();
    _bounds.assign(_glyph_count, GlyphBounds{});
    _bounds_known.assign(_glyph_count, 0);
    _loaded = true;
    return true;
}

bool OpenTypeFont::read_directory(std::string* why, uint32_t face_index)
{
    bool bad = false;
    const Reader file{_data, _size, &bad};
    uint32_t version = file.u32(0);
    if (bad)
        return fail(why, "the file is too short to be a font");
    // A collection is a list of table directories in one file; a face's
    // tables are found from the start of the file like any font's.
    size_t directory = 0;
    uint32_t faces = 1;
    if (version == tag_of("ttcf")) {
        faces = file.u32(8);
        if (bad || !file.has(12, size_t(faces) * 4))
            return fail(why, "the font collection's header is truncated");
        if (faces == 0)
            return fail(why, "the font collection holds no faces");
        if (face_index >= faces)
            return fail(why, "face index " + std::to_string(face_index) +
                                 " was asked of a font collection that holds " +
                                 std::to_string(faces) + (faces == 1 ? " face" : " faces"));
        directory = file.u32(12 + size_t(face_index) * 4);
        version = file.u32(directory);
        if (bad)
            return fail(why, "the font collection points outside the file for this face");
    } else if (face_index != 0) {
        return fail(why, "face index " + std::to_string(face_index) +
                             " was asked of a file that is not a font collection and holds "
                             "one face");
    }
    // What is recognised and not read is named, so that the host knows what
    // it has.
    if (version == tag_of("wOFF"))
        return fail(why, "WOFF fonts are not supported: this is a compressed web font, and "
                         "the OpenType font inside it has to be unpacked first");
    if (version == tag_of("wOF2"))
        return fail(why, "WOFF2 fonts are not supported: this is a compressed web font, and "
                         "the OpenType font inside it has to be unpacked first");
    const unsigned b0 = _data[0], b1 = _size > 1 ? _data[1] : 0u;
    if (version == tag_of("typ1") || (b0 == '%' && b1 == '!') || (b0 == 0x80 && b1 == 0x01))
        return fail(why, "Type 1 (PostScript) fonts are not supported");
    if (version != 0x00010000u && version != tag_of("true") && version != tag_of("OTTO"))
        return fail(why, "not an OpenType or TrueType font");
    const size_t count = file.u16(directory + 4);
    if (bad || !file.has(directory + 12, count * 16))
        return fail(why, "the table directory is truncated");
    _sfnt_version = version;
    _face_index = face_index;
    _face_count = faces;
    const size_t records = directory + 12;
    // A tag named twice would let two readers of the file take different
    // tables for the same name.
    std::vector<uint32_t> tags(count);
    for (size_t i = 0; i < count; ++i)
        tags[i] = file.u32(records + i * 16);
    std::sort(tags.begin(), tags.end());
    if (std::adjacent_find(tags.begin(), tags.end()) != tags.end())
        return fail(why, "the table directory names a table twice");
    _directory.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        DirectoryEntry entry;
        entry.tag = file.u32(records + i * 16);
        entry.offset = file.u32(records + i * 16 + 8);
        entry.length = file.u32(records + i * 16 + 12);
        // A table that runs past the end of the file is treated as absent, so
        // that a file cut short fails only if a table that is needed is gone.
        if (!file.has(entry.offset, entry.length)) {
            warn("a table extends past the end of the file and was ignored");
            continue;
        }
        _directory.push_back(entry);
        _table_bytes += entry.length;
    }
    return true;
}

bool OpenTypeFont::read_head_maxp_hhea(std::string* why)
{
    bool bad = false;
    const Table head = table("head"), maxp = table("maxp"), hhea = table("hhea");
    if (!head.present() || !maxp.present() || !hhea.present())
        return fail(why, "a required table (head, maxp or hhea) is missing");
    const Reader h{head.data, head.size, &bad}, m{maxp.data, maxp.size, &bad},
        a{hhea.data, hhea.size, &bad};
    _units_per_em = h.u16(18);
    _index_to_loc_format = h.s16(50);
    _glyph_count = m.u16(4);
    _ascender = a.s16(4);
    _descender = a.s16(6);
    _line_gap = a.s16(8);
    _hmetric_count = a.u16(34);
    if (bad)
        return fail(why, "head, maxp or hhea is truncated");
    if (_units_per_em == 0)
        return fail(why, "head.unitsPerEm is zero");
    if (_glyph_count == 0)
        return fail(why, "the font has no glyphs");
    return true;
}

void OpenTypeFont::read_os2()
{
    const Table os2 = table("OS/2");
    if (!os2.present())
        return;
    bool bad = false;
    const Reader r{os2.data, os2.size, &bad};
    const unsigned version = r.u16(0);
    if (bad || version < 2)
        return;
    const int x_height = r.s16(86);
    if (!bad)
        _x_height = x_height;
}

bool OpenTypeFont::read_hmtx(std::string* why)
{
    const Table hmtx = table("hmtx");
    if (!hmtx.present())
        return fail(why, "the hmtx table is missing");
    bool bad = false;
    const Reader r{hmtx.data, hmtx.size, &bad};
    const uint32_t long_count = std::min(_hmetric_count, _glyph_count);
    if (!r.has(0, size_t(long_count) * 4))
        return fail(why, "the hmtx table is truncated");
    _advances.resize(_glyph_count);
    _lsbs.resize(_glyph_count);
    for (uint32_t g = 0; g < long_count; ++g) {
        _advances[g] = r.u16(size_t(g) * 4);
        _lsbs[g] = r.s16(size_t(g) * 4 + 2);
    }
    // Glyphs past the last full record share its advance and have a side
    // bearing each; a missing side bearing is zero. With no full record at
    // all there is no advance to share and every glyph has none.
    if (long_count == 0)
        warn("hhea.numberOfHMetrics is zero; every glyph has no advance");
    const uint16_t shared = long_count > 0 ? _advances[long_count - 1] : uint16_t(0);
    const size_t tail = size_t(long_count) * 4;
    for (uint32_t g = long_count; g < _glyph_count; ++g) {
        _advances[g] = shared;
        const size_t at = tail + size_t(g - long_count) * 2;
        _lsbs[g] = r.has(at, 2) ? r.s16(at) : int16_t(0);
    }
    return true;
}

namespace
{
// How a cmap subtable's character codes become Unicode codepoints.
enum CmapCodes : int {
    CMAP_UNICODE = 0,   // they are codepoints
    CMAP_SYMBOL = 1,    // Windows Symbol: taken as they stand
    CMAP_MAC_ROMAN = 2, // Macintosh Roman: one byte, through MAC_ROMAN_HIGH
};
} // namespace

bool OpenTypeFont::read_cmap(std::string* why)
{
    const Table cmap = table("cmap");
    if (!cmap.present())
        return fail(why, "the cmap table is missing");
    bool bad = false;
    const Reader r{cmap.data, cmap.size, &bad};
    const size_t count = r.u16(2);
    if (bad || !r.has(4, count * 8))
        return fail(why, "the cmap table is truncated");

    // fontTools' getBestCmap(): the first of the Unicode (platform, encoding)
    // pairs that the font has. Beyond it, two that are not Unicode, for a
    // font with nothing else.
    struct Wanted {
        int platform, encoding, codes;
    };
    static const Wanted preference[] = {
        {3, 10, CMAP_UNICODE}, {0, 6, CMAP_UNICODE},  {0, 4, CMAP_UNICODE}, {3, 1, CMAP_UNICODE},
        {0, 3, CMAP_UNICODE},  {0, 2, CMAP_UNICODE},  {0, 1, CMAP_UNICODE}, {0, 0, CMAP_UNICODE},
        {3, 0, CMAP_SYMBOL},   {1, 0, CMAP_MAC_ROMAN}};
    bool passed_over = false;
    for (const Wanted& wanted : preference) {
        // The first record with this platform and encoding stands for it.
        for (size_t i = 0; i < count; ++i) {
            if (r.u16(4 + i * 8) != wanted.platform || r.u16(6 + i * 8) != wanted.encoding)
                continue;
            const int outcome = read_cmap_subtable(cmap, r.u32(8 + i * 8), wanted.codes, why);
            if (outcome < 0)
                return false;
            if (outcome > 0) {
                _cmap_platform = wanted.platform;
                _cmap_encoding = wanted.encoding;
                if (passed_over)
                    warn("the preferred cmap subtable is in a format that is not read; another "
                         "was used");
                if (wanted.codes != CMAP_UNICODE)
                    warn("the font has no Unicode cmap subtable; its Symbol or Macintosh Roman "
                         "one was used");
                return true;
            }
            passed_over = true;
            break;
        }
    }
    return fail(why, passed_over
                         ? "the font's cmap subtables are in formats that are not read (0, 4, 6, "
                           "10, 12 and 13 are)"
                         : "the font has no Unicode, Symbol or Macintosh Roman cmap subtable");
}

int OpenTypeFont::read_cmap_subtable(const Table& cmap, size_t offset, int codes, std::string* why)
{
    bool bad = false;
    const Reader r{cmap.data, cmap.size, &bad};
    const auto malformed = [&](const char* reason) {
        fail(why, reason);
        return -1;
    };
    if (!r.has(offset, 4))
        return malformed("the cmap subtable is truncated");
    Reader s = r.from(offset);
    const int format = s.u16(0);
    if (format != 0 && format != 4 && format != 6 && format != 10 && format != 12 && format != 13)
        return 0;
    // The formats past 16 bits are Unicode's; a one-byte encoding has no use
    // for them.
    if (codes == CMAP_MAC_ROMAN && format >= 10)
        return 0;
    _cmap.clear();

    // Code to glyph for the formats whose codes fit 16 bits, where a later
    // entry replaces an earlier one, as building a dict from them does.
    constexpr uint32_t UNMAPPED = 0xFFFFFFFFu;
    std::vector<uint32_t> bmp;
    // One more codepoint for the formats that are read in ascending order.
    const auto append = [&](uint32_t cp, uint32_t glyph) {
        if (!_cmap.empty() && _cmap.back().last + 1 == cp &&
            _cmap.back().glyph + (cp - _cmap.back().first) == glyph)
            _cmap.back().last = cp;
        else
            _cmap.push_back(CmapRange{cp, cp, glyph, false});
    };

    if (format == 0) {
        if (!s.has(6, 256))
            return malformed("the cmap format 0 subtable is truncated");
        bmp.assign(65536, UNMAPPED);
        for (uint32_t code = 0; code < 256; ++code)
            bmp[code] = s.u8(6 + code);
    } else if (format == 4) {
        const size_t length = s.u16(2);
        if (length >= 16 && length <= s.n)
            s.n = length;
        const size_t seg_x2 = s.u16(6);
        const size_t segments = seg_x2 / 2;
        const size_t end_at = 14, start_at = 16 + seg_x2, delta_at = 16 + 2 * seg_x2,
                     range_at = 16 + 3 * seg_x2, array_at = 16 + 4 * seg_x2;
        if (bad || segments == 0 || !s.has(0, array_at))
            return malformed("the cmap format 4 subtable is truncated");
        const size_t array_count = (s.n - array_at) / 2;
        bmp.assign(65536, UNMAPPED);
        size_t work = 0;
        // Every segment but the last, which is the 0xFFFF terminator.
        for (size_t i = 0; i + 1 < segments; ++i) {
            const uint32_t start = s.u16(start_at + i * 2);
            const uint32_t end = s.u16(end_at + i * 2);
            const uint32_t delta = s.u16(delta_at + i * 2);
            const size_t range_offset = s.u16(range_at + i * 2);
            for (uint32_t cp = start; cp <= end; ++cp) {
                if (++work > MAX_CMAP4_WORK)
                    return malformed("the cmap format 4 subtable's segments overlap");
                uint32_t glyph;
                if (range_offset == 0) {
                    glyph = (cp + delta) & 0xFFFFu;
                } else {
                    // fontTools' index: charCode + rangeOffset/2 - start + i - segCount.
                    // See the UPSTREAM NOTICE above BoundsPen.
                    const size_t index = range_offset / 2 + (cp - start) + i;
                    if (index < segments || index - segments >= array_count) {
                        warn("a cmap format 4 segment points outside its glyph array");
                        continue;
                    }
                    const uint32_t raw = s.u16(array_at + (index - segments) * 2);
                    glyph = raw != 0 ? (raw + delta) & 0xFFFFu : 0;
                }
                bmp[cp] = glyph;
            }
        }
    } else if (format == 6) {
        const uint32_t first = s.u16(6);
        const size_t entries = s.u16(8);
        if (bad || !s.has(10, entries * 2))
            return malformed("the cmap format 6 subtable is truncated");
        bmp.assign(65536, UNMAPPED);
        for (size_t i = 0; i < entries && first + i < 65536; ++i)
            bmp[first + i] = s.u16(10 + i * 2);
    } else if (format == 10) {
        // A trimmed array of 32-bit codes: the first code, a count, a glyph each.
        const uint64_t first = s.u32(12);
        const size_t entries = s.u32(16);
        if (bad || s.n < 20 || entries > (s.n - 20) / 2)
            return malformed("the cmap format 10 subtable is truncated");
        for (size_t i = 0; i < entries && first + i <= 0x10FFFFu; ++i) {
            const uint32_t glyph = s.u16(20 + i * 2);
            if (glyph != 0)
                append(static_cast<uint32_t>(first + i), glyph);
        }
    } else {
        // Formats 12 and 13: groups of (first code, last code, glyph). In 12
        // the glyph ids run on through the group; in 13 every code of the
        // group has the one glyph. Groups are taken as fontTools takes them
        // (UPSTREAM NOTICE above BoundsPen): cut off at U+10FFFF; left out if
        // inverted or starting before the last one taken ends; one that
        // starts exactly where that one ends replaces its last code.
        const size_t groups = s.u32(12);
        if (bad || s.n < 16 || groups > (s.n - 16) / 12)
            return malformed(format == 12 ? "the cmap format 12 subtable is truncated"
                                          : "the cmap format 13 subtable is truncated");
        uint32_t last_end = 0;
        for (size_t i = 0; i < groups; ++i) {
            uint32_t first = s.u32(16 + i * 12);
            const uint32_t last = std::min<uint32_t>(s.u32(20 + i * 12), 0x10FFFFu);
            uint32_t glyph = s.u32(24 + i * 12);
            if (first > last || first < last_end) {
                warn("a cmap format 12 or 13 group is out of order or overlaps and was ignored");
                continue;
            }
            last_end = last;
            if (glyph == 0) {
                // The missing glyph maps nothing: none of a format 13 group,
                // and not the first code of a format 12 one.
                if (format == 13 || first == last)
                    continue;
                ++first;
                ++glyph;
            }
            if (!_cmap.empty() && _cmap.back().last >= first) {
                if (_cmap.back().first >= first)
                    _cmap.pop_back();
                else
                    _cmap.back().last = first - 1;
            }
            if (format == 12) {
                // The ids of a group are 32-bit and do not wrap: a code whose
                // id would pass 2^32 - 1 is one the font does not map.
                uint32_t mapped_last = last;
                if (last - first > 0xFFFFFFFFu - glyph) {
                    mapped_last = first + (0xFFFFFFFFu - glyph);
                    warn("a cmap format 12 group's glyph ids run past 32 bits; the codes past "
                         "the last id were left unmapped");
                }
                _cmap.push_back(CmapRange{first, mapped_last, glyph, false});
            } else {
                // Kept as the one group it is: a group may span every code
                // there is, and a range a code would be a megabyte a plane.
                _cmap.push_back(CmapRange{first, last, glyph, true});
            }
        }
    }

    if (!bmp.empty() && codes == CMAP_MAC_ROMAN) {
        std::vector<uint32_t> unicode(65536, UNMAPPED);
        for (uint32_t code = 0; code < 256; ++code)
            if (bmp[code] != UNMAPPED)
                unicode[code < 128 ? code : MAC_ROMAN_HIGH[code - 128]] = bmp[code];
        bmp.swap(unicode);
    }
    // A code mapped to glyph 0 is a code the font does not have.
    for (uint32_t cp = 0; cp < bmp.size(); ++cp)
        if (bmp[cp] != UNMAPPED && bmp[cp] != 0)
            append(cp, bmp[cp]);
    _cmap_format = format;
    return 1;
}

bool OpenTypeFont::lookup(uint32_t codepoint, uint32_t* glyph) const
{
    auto it = std::upper_bound(_cmap.begin(), _cmap.end(), codepoint,
                               [](uint32_t cp, const CmapRange& r) { return cp < r.first; });
    if (it == _cmap.begin())
        return false;
    --it;
    if (codepoint > it->last)
        return false;
    if (glyph != nullptr)
        *glyph = it->glyph_of(codepoint);
    return true;
}

bool OpenTypeFont::read_outline_tables(std::string* why)
{
    _glyf = table("glyf");
    const Table loca = table("loca");
    _cff = table("CFF ");
    if (_glyf.present() && loca.present()) {
        bool bad = false;
        const Reader r{loca.data, loca.size, &bad};
        const size_t entries = size_t(_glyph_count) + 1;
        const bool is_long = _index_to_loc_format != 0;
        const size_t width = is_long ? 4 : 2;
        // The entry after the last glyph's says where that glyph ends. A table
        // that stops one short of it is read with glyf's own end standing in.
        _loca_unterminated = !r.has(0, entries * width) && r.has(0, (entries - 1) * width);
        if (!_loca_unterminated && !r.has(0, entries * width))
            return fail(why, "the loca table is truncated");
        _loca.resize(entries);
        const size_t stated = _loca_unterminated ? entries - 1 : entries;
        for (size_t i = 0; i < stated; ++i)
            _loca[i] = is_long ? r.u32(i * 4) : uint32_t(r.u16(i * 2)) * 2;
        if (_loca_unterminated) {
            _loca[entries - 1] = static_cast<uint32_t>(_glyf.size);
            warn("the loca table has no entry after the last glyph's; glyf's end was used");
        }
        _cff = Table{};
        return true;
    }
    _glyf = Table{};
    if (_cff.present()) {
        _is_cff = true;
        return read_cff(why);
    }
    if (table("CFF2").present())
        return fail(why, "CFF2 outlines (a variable CFF font) are not supported");
    if (table("CBDT").present() || table("sbix").present() || table("EBDT").present() ||
        table("bdat").present())
        return fail(why, "the font has only bitmap glyphs (CBDT, sbix or EBDT) and no outlines, "
                         "and bitmap fonts are not supported");
    if (table("SVG ").present() || table("COLR").present())
        return fail(why, "the font has only colour glyphs (SVG or COLR) and no glyf or CFF "
                         "outlines");
    return fail(why, "the font has neither glyf nor CFF outlines");
}

// What a Private DICT of `size_value` bytes at `offset_value` says about local
// subroutines: `*subrs_at` is where their INDEX starts in the table, or 0 if
// the DICT names none that can be read. Returns what in it a rasteriser might
// read differently from this reader, or null.
const char* OpenTypeFont::read_cff_private(double size_value, double offset_value, size_t* subrs_at)
{
    *subrs_at = 0;
    bool bad = false;
    const Reader cff{_cff.data, _cff.size, &bad};
    size_t private_offset = 0, private_size = 0;
    CffDict priv;
    if (!as_offset(offset_value, cff.n, private_offset) ||
        !as_offset(size_value, cff.n - private_offset, private_size) ||
        !read_dict(Reader{cff.p + private_offset, private_size, &bad}, priv)) {
        warn("the CFF Private DICT is malformed and was ignored");
        return "the CFF Private DICT is malformed";
    }
    size_t subrs = 0;
    const bool in_table = priv.has_subrs && as_offset(priv.subrs, cff.n - private_offset, subrs);
    if (priv.has_subrs && (!in_table || subrs == 0))
        return "the CFF local subroutines are outside the table";
    if (in_table)
        *subrs_at = private_offset + subrs;
    if (!priv.unambiguous)
        return "the CFF Private DICT states an offset twice or with the wrong operands";
    if (private_offset == 0 && private_size != 0)
        return "the CFF Private DICT is at offset zero";
    return nullptr;
}

bool OpenTypeFont::read_cff(std::string* why)
{
    bool bad = false;
    const Reader cff{_cff.data, _cff.size, &bad};
    const unsigned major = cff.u8(0);
    const size_t header_size = cff.u8(2);
    if (bad || major != 1)
        return fail(why, "the CFF table is not version 1");
    std::vector<uint32_t> names, tops;
    size_t at = header_size;
    if (!read_index(cff, at, names, at) || !read_index(cff, at, tops, at) || tops.size() < 2)
        return fail(why, "the CFF table's Name or Top DICT index is malformed");
    if (!read_index(cff, at, _string_offsets, at) || !read_index(cff, at, _global_subr_offsets, at))
        return fail(why, "the CFF table's String or Global Subr index is malformed");

    CffDict top;
    if (!read_dict(Reader{cff.p + tops[0], tops[1] - tops[0], &bad}, top))
        return fail(why, "the CFF Top DICT is malformed");
    _is_cid = top.is_cid;
    if (!top.unambiguous)
        _cff_tolerated = "the CFF Top DICT states an offset twice or with the wrong operands";
    else if (top.has_fd && !top.is_cid)
        _cff_tolerated = "the CFF font has per-glyph Font DICTs without being CID-keyed";
    if (top.charstring_type != 2)
        return fail(why, "the CFF font's charstrings are not Type 2");
    size_t charstrings = 0;
    if (!top.has_charstrings || !as_offset(top.charstrings, cff.n, charstrings) ||
        !read_index(cff, charstrings, _charstring_offsets, at) || _charstring_offsets.size() < 2)
        return fail(why, "the CFF CharStrings index is missing or malformed");
    const size_t glyphs = _charstring_offsets.size() - 1;

    // Local subroutines. Private DICTs may name the same INDEX, which is read
    // once; INDEXes that overlap without being the same are bounded by the
    // table's size, which those of an honest font cannot exceed between them.
    std::vector<size_t> sets_at;
    size_t offsets_kept = 0;
    const auto local_subrs = [&](size_t subrs_at) -> uint16_t {
        for (size_t i = 0; i < sets_at.size(); ++i)
            if (sets_at[i] == subrs_at)
                return static_cast<uint16_t>(i);
        std::vector<uint32_t> offsets;
        size_t end = 0;
        if (!read_index(cff, subrs_at, offsets, end)) {
            offsets.clear();
            warn("the CFF local subroutine index is malformed and was ignored");
            _cff_tolerated = "the CFF local subroutine index is malformed";
        } else if (offsets.size() > cff.n - std::min(cff.n, offsets_kept)) {
            offsets.clear();
            warn("the CFF local subroutine indexes overlap; some were ignored");
            _cff_tolerated = "the CFF local subroutine indexes overlap";
        }
        offsets_kept += offsets.size();
        sets_at.push_back(subrs_at);
        _local_subr_offsets.push_back(std::move(offsets));
        return static_cast<uint16_t>(_local_subr_offsets.size() - 1);
    };
    // Set 0 is "no local subroutines".
    sets_at.push_back(0);
    _local_subr_offsets.emplace_back();

    if (!_is_cid) {
        // One Private DICT, named by the Top DICT, for every glyph.
        _fd_subrs.assign(1, 0);
        if (top.has_private) {
            size_t subrs_at = 0;
            if (const char* tolerated =
                    read_cff_private(top.private_size, top.private_offset, &subrs_at))
                _cff_tolerated = tolerated;
            if (subrs_at != 0)
                _fd_subrs[0] = local_subrs(subrs_at);
        }
    } else {
        // CID-keyed: the Top DICT names an INDEX of Font DICTs, each with a
        // Private DICT of its own, and FDSelect says which a glyph uses.
        size_t fd_array = 0, fd_select = 0;
        std::vector<uint32_t> dicts;
        if (!top.has_fd_array || !as_offset(top.fd_array, cff.n, fd_array) || fd_array == 0 ||
            !read_index(cff, fd_array, dicts, at) || dicts.size() < 2)
            return fail(why, "the CID-keyed CFF font's Font DICT index is missing or malformed");
        const size_t fd_count = dicts.size() - 1;
        if (fd_count > CFF_MAX_FONT_DICTS)
            return fail(why, "the CID-keyed CFF font has more Font DICTs than FDSelect can name");
        _fd_subrs.assign(fd_count, 0);
        for (size_t fd = 0; fd < fd_count; ++fd) {
            CffDict dict;
            if (!read_dict(Reader{cff.p + dicts[fd], dicts[fd + 1] - dicts[fd], &bad}, dict)) {
                warn("a CFF Font DICT is malformed and was ignored");
                _cff_tolerated = "a CFF Font DICT is malformed";
                continue;
            }
            if (!dict.has_private)
                continue;
            if (!dict.unambiguous)
                _cff_tolerated = "a CFF Font DICT states an offset twice or with the wrong "
                                 "operands";
            size_t subrs_at = 0;
            if (const char* tolerated =
                    read_cff_private(dict.private_size, dict.private_offset, &subrs_at))
                _cff_tolerated = tolerated;
            if (subrs_at != 0)
                _fd_subrs[fd] = local_subrs(subrs_at);
        }

        if (!top.has_fd_select || !as_offset(top.fd_select, cff.n, fd_select) || fd_select == 0)
            return fail(why, "the CID-keyed CFF font's FDSelect is missing");
        bool select_bad = false;
        const Reader f{cff.p + fd_select, cff.n - fd_select, &select_bad};
        _fd_select.assign(glyphs, CFF_NO_FONT_DICT);
        const unsigned format = f.u8(0);
        if (format == 0) {
            // A Font DICT per glyph.
            if (f.need(1, glyphs))
                for (size_t g = 0; g < glyphs; ++g)
                    _fd_select[g] = f.u8(1 + g);
        } else if (format == 3) {
            // Ranges of glyphs, each from its first glyph to the next range's,
            // and a last glyph id closing the last range.
            const size_t ranges = f.u16(1);
            if (ranges == 0 || !f.need(3, ranges * 3 + 2) || f.u16(3) != 0)
                select_bad = true;
            for (size_t i = 0; i < ranges && !select_bad; ++i) {
                const size_t first = f.u16(3 + i * 3), next = f.u16(6 + i * 3);
                const uint16_t fd = f.u8(5 + i * 3);
                if (next <= first) {
                    select_bad = true;
                    break;
                }
                for (size_t g = first; g < next && g < glyphs; ++g)
                    _fd_select[g] = fd;
            }
        } else {
            select_bad = true;
        }
        if (select_bad)
            return fail(why, "the CID-keyed CFF font's FDSelect is malformed");
    }

    // The charset: a string id per glyph, which is where glyph names come
    // from; for a CID-keyed font a CID per glyph.
    _charset.assign(glyphs, 0);
    size_t charset = 0;
    const bool custom = top.has_charset && as_offset(top.charset, cff.n, charset) && charset > 2;
    if (!custom) {
        // The predefined charsets: 0 is ISOAdobe, in which a glyph's id is
        // its string id; 1 and 2 are Expert and Expert Subset. A CID-keyed
        // font with none of its own numbers its glyphs as they come.
        if (_is_cid) {
            for (size_t g = 0; g < glyphs; ++g)
                _charset[g] = static_cast<uint16_t>(std::min<size_t>(g, 0xFFFFu));
        } else if (charset == 0) {
            for (size_t g = 0; g < glyphs && g <= 228; ++g)
                _charset[g] = static_cast<uint16_t>(g);
        } else {
            const uint16_t* table = charset == 1 ? CFF_EXPERT_CHARSET : CFF_EXPERT_SUBSET_CHARSET;
            const size_t count =
                charset == 1 ? std::size(CFF_EXPERT_CHARSET) : std::size(CFF_EXPERT_SUBSET_CHARSET);
            for (size_t g = 0; g < glyphs && g < count; ++g)
                _charset[g] = table[g];
        }
        return true;
    }
    bool charset_bad = false;
    const Reader c{cff.p + charset, cff.n - charset, &charset_bad};
    const unsigned format = c.u8(0);
    size_t g = 1, pos = 1;
    if (format == 0) {
        for (; g < glyphs && !charset_bad; ++g, pos += 2)
            _charset[g] = c.u16(pos);
    } else if (format == 1 || format == 2) {
        while (g < glyphs && !charset_bad) {
            const uint32_t first = c.u16(pos);
            const uint32_t left = format == 1 ? c.u8(pos + 2) : c.u16(pos + 2);
            pos += format == 1 ? 3 : 4;
            for (uint32_t k = 0; k <= left && g < glyphs; ++k, ++g)
                _charset[g] = static_cast<uint16_t>((first + k) & 0xFFFFu);
        }
    } else {
        charset_bad = true;
    }
    if (charset_bad) {
        std::fill(_charset.begin(), _charset.end(), uint16_t(0));
        _charset_unread = true;
        warn("the CFF charset is malformed; glyph names are not read");
    }
    return true;
}

std::string_view OpenTypeFont::glyph_name(uint32_t glyph) const
{
    if (!_is_cff || _is_cid || glyph >= _charset.size())
        return {};
    const size_t sid = _charset[glyph];
    if (glyph != 0 && sid == 0)
        return {};
    if (sid < CFF_STANDARD_STRING_COUNT)
        return CFF_STANDARD_STRINGS[sid];
    const size_t index = sid - CFF_STANDARD_STRING_COUNT;
    if (index + 1 >= _string_offsets.size())
        return {};
    const uint32_t begin = _string_offsets[index], end = _string_offsets[index + 1];
    return std::string_view(reinterpret_cast<const char*>(_cff.data + begin), end - begin);
}

bool OpenTypeFont::glyph_order_names(std::vector<std::string>& names) const
{
    names.clear();
    if (!_loaded)
        return false;
    if (_is_cff) {
        if (_charset_unread)
            return false;
        names.resize(_glyph_count);
        for (uint32_t g = 0; g < _glyph_count; ++g) {
            if (!_is_cid) {
                names[g] = std::string(glyph_name(g));
            } else if (g == 0) {
                names[g] = ".notdef";
            } else {
                char cid[16];
                std::snprintf(cid, sizeof cid, "cid%05u",
                              static_cast<unsigned>(g < _charset.size() ? _charset[g] : 0u));
                names[g] = cid;
            }
        }
        return true;
    }

    const Table post = table("post");
    if (!post.present())
        return false;
    bool bad = false;
    const Reader r{post.data, post.size, &bad};
    const uint32_t version = r.u32(0);
    if (bad)
        return false;
    if (version == 0x00010000u) {
        // The standard Macintosh order itself, which names 258 glyphs.
        if (_glyph_count > MAC_GLYPH_NAME_COUNT)
            return false;
        for (uint32_t g = 0; g < _glyph_count; ++g)
            names.emplace_back(MAC_GLYPH_NAMES[g]);
        return true;
    }
    if (version != 0x00020000u)
        return false;
    // Format 2: a number per glyph, below 258 a standard name and from 258 on
    // one of the Pascal strings that follow the numbers, in order.
    const size_t stated = std::min<size_t>(r.u16(32), _glyph_count);
    if (bad || stated == 0 || !r.has(34, stated * 2))
        return false;
    size_t highest = 0;
    for (size_t g = 0; g < stated; ++g)
        highest = std::max<size_t>(highest, r.u16(34 + g * 2));
    std::vector<std::string_view> extra(highest > 257 ? highest - 257 : 0);
    size_t at = 34 + stated * 2;
    for (std::string_view& name : extra) {
        // A string that runs past the table is an empty name.
        const size_t length = at < post.size ? post.data[at] : 0u;
        ++at;
        if (at + length <= post.size)
            name = std::string_view(reinterpret_cast<const char*>(post.data + at), length);
        at += length;
    }
    names.resize(_glyph_count);
    for (size_t g = 0; g < stated; ++g) {
        const size_t index = r.u16(34 + g * 2);
        names[g] = std::string(index < MAC_GLYPH_NAME_COUNT ? MAC_GLYPH_NAMES[index]
                                                            : extra[index - MAC_GLYPH_NAME_COUNT]);
    }
    // fontTools' glyph order has to name every glyph differently, and how it
    // does so decides where a glyph sorts (UPSTREAM NOTICE above BoundsPen):
    // a glyph with no name is "glyphNNNNN"; a name already given to an
    // earlier glyph gets ".1", ".2", ... counting past any such name the
    // table itself uses.
    std::unordered_set<std::string> stated_names(names.begin(), names.end());
    std::unordered_map<std::string, uint32_t> given;
    for (uint32_t g = 0; g < _glyph_count; ++g) {
        std::string name = names[g];
        if (name.empty()) {
            char made[16];
            std::snprintf(made, sizeof made, "glyph%05u", static_cast<unsigned>(g));
            name = made;
        }
        const auto seen = given.find(name);
        if (seen != given.end()) {
            uint32_t n = seen->second;
            while (stated_names.count(name + "." + std::to_string(n)) != 0)
                ++n;
            seen->second = n + 1;
            name += "." + std::to_string(n);
        }
        given[name] = 1;
        names[g] = std::move(name);
    }
    return true;
}

const std::vector<uint32_t>* OpenTypeFont::local_subrs_of(uint32_t glyph) const
{
    size_t fd = 0;
    if (_is_cid) {
        if (glyph >= _fd_select.size())
            return nullptr;
        fd = _fd_select[glyph];
    }
    if (fd >= _fd_subrs.size())
        return nullptr;
    return &_local_subr_offsets[_fd_subrs[fd]];
}

// --- Outlines: glyf ------------------------------------------------------------------

// `transforms` is `depth` affine transforms of six doubles each, outermost
// first. A point is put through them innermost first, one at a time, as nested
// fontTools TransformPens do; they are not multiplied together, because that
// would round differently.
bool OpenTypeFont::draw_glyf(uint32_t glyph, BoundsPen& pen, const double* transforms, int depth,
                             bool top_level, int& budget) const
{
    if (glyph >= _glyph_count)
        return false;
    const size_t begin = _loca[glyph], end = _loca[glyph + 1];
    if (end <= begin)
        return end == begin; // no data: a glyph with no outline
    if (end > _glyf.size)
        return false;
    bool bad = false;
    const Reader g{_glyf.data + begin, end - begin, &bad};
    const int contours = g.s16(0);
    const int x_min = g.s16(2);
    if (bad || !g.has(0, 10))
        return false;
    if (contours == 0)
        return true;

    if (contours < 0) {
        size_t at = 10;
        for (;;) {
            if ((budget -= GLYF_COMPONENT_COST) < 0)
                return false;
            const unsigned flags = g.u16(at);
            const uint32_t child = g.u16(at + 2);
            at += 4;
            double dx = 0, dy = 0;
            if (flags & 0x0001) {
                dx = g.s16(at);
                dy = g.s16(at + 2);
                at += 4;
            } else {
                dx = static_cast<int8_t>(g.u8(at));
                dy = static_cast<int8_t>(g.u8(at + 1));
                at += 2;
            }
            double xx = 1, xy = 0, yx = 0, yy = 1;
            if (flags & 0x0008) {
                xx = yy = g.s16(at) / 16384.0;
                at += 2;
            } else if (flags & 0x0040) {
                xx = g.s16(at) / 16384.0;
                yy = g.s16(at + 2) / 16384.0;
                at += 4;
            } else if (flags & 0x0080) {
                xx = g.s16(at) / 16384.0;
                xy = g.s16(at + 2) / 16384.0;
                yx = g.s16(at + 4) / 16384.0;
                yy = g.s16(at + 6) / 16384.0;
                at += 8;
            }
            if (bad)
                return false;
            if (!(flags & 0x0002)) {
                // The arguments are point numbers to match, which fontTools
                // cannot draw either.
                warn("a composite glyph places a component by point matching; it was skipped");
            } else if (depth >= MAX_COMPONENT_DEPTH) {
                return false;
            } else {
                double chain[(MAX_COMPONENT_DEPTH + 1) * 6];
                if (depth > 0)
                    std::memcpy(chain, transforms, sizeof(double) * 6 * static_cast<size_t>(depth));
                double* t = chain + 6 * depth;
                t[0] = xx, t[1] = xy, t[2] = yx, t[3] = yy, t[4] = dx, t[5] = dy;
                if (!draw_glyf(child, pen, chain, depth + 1, false, budget))
                    return false;
            }
            if (!(flags & 0x0020))
                return true;
        }
    }

    // A simple glyph. fontTools moves a glyph drawn at top level so that its
    // left edge is at the hmtx side bearing; a component is never moved.
    const size_t n_contours = static_cast<size_t>(contours);
    if (!g.has(10, n_contours * 2 + 2))
        return false;
    size_t n_points = 0;
    for (size_t i = 0; i < n_contours; ++i) {
        const size_t last = g.u16(10 + i * 2);
        if (last + 1 <= n_points)
            return false; // end points must increase
        n_points = last + 1;
    }
    if ((budget -= static_cast<int>(n_points)) < 0)
        return false;
    size_t at = 10 + n_contours * 2;
    at += 2 + g.u16(at);
    std::vector<uint8_t> flags(n_points);
    for (size_t i = 0; i < n_points;) {
        const uint8_t flag = g.u8(at++);
        flags[i++] = flag;
        if (flag & 0x08) {
            size_t repeat = g.u8(at++);
            for (; repeat > 0 && i < n_points; --repeat)
                flags[i++] = flag;
        }
        if (bad)
            return false;
    }
    struct GlyfPoint {
        double x, y;
    };
    std::vector<GlyfPoint> points(n_points);
    int value = 0;
    for (size_t i = 0; i < n_points; ++i) {
        if (flags[i] & 0x02) {
            const int d = g.u8(at++);
            value += (flags[i] & 0x10) ? d : -d;
        } else if (!(flags[i] & 0x10)) {
            value += g.s16(at);
            at += 2;
        }
        points[i].x = value;
    }
    value = 0;
    for (size_t i = 0; i < n_points; ++i) {
        if (flags[i] & 0x04) {
            const int d = g.u8(at++);
            value += (flags[i] & 0x20) ? d : -d;
        } else if (!(flags[i] & 0x20)) {
            value += g.s16(at);
            at += 2;
        }
        points[i].y = value;
    }
    if (bad)
        return false;

    const int shift = top_level ? left_side_bearing(glyph) - x_min : 0;
    for (GlyfPoint& p : points) {
        double x = p.x + shift, y = p.y;
        for (int level = depth - 1; level >= 0; --level) {
            const double* t = transforms + 6 * level;
            const double tx = t[0] * x + t[2] * y + t[4];
            const double ty = t[1] * x + t[3] * y + t[5];
            x = tx;
            y = ty;
        }
        p.x = x;
        p.y = y;
    }

    size_t first = 0;
    for (size_t c = 0; c < n_contours; ++c) {
        const size_t last = g.u16(10 + c * 2);
        const size_t count = last + 1 - first;
        const GlyfPoint* pts = points.data() + first;
        const uint8_t* on = flags.data() + first;
        first = last + 1;

        size_t first_on = count;
        for (size_t i = 0; i < count; ++i) {
            if (on[i] & 0x01) {
                first_on = i;
                break;
            }
        }
        if (first_on == count) {
            // No on-curve point: start midway between the last and first.
            const double sx = 0.5 * (pts[count - 1].x + pts[0].x);
            const double sy = 0.5 * (pts[count - 1].y + pts[0].y);
            pen.move_to(sx, sy);
            for (size_t i = 0; i + 1 < count; ++i)
                pen.quad_to(pts[i].x, pts[i].y, 0.5 * (pts[i].x + pts[i + 1].x),
                            0.5 * (pts[i].y + pts[i + 1].y));
            pen.quad_to(pts[count - 1].x, pts[count - 1].y, sx, sy);
            continue;
        }
        // Start at the first on-curve point and go once round, ending on it.
        pen.move_to(pts[first_on].x, pts[first_on].y);
        bool pending = false;
        GlyfPoint off{0, 0};
        for (size_t k = 1; k <= count; ++k) {
            const size_t i = (first_on + k) % count;
            if (on[i] & 0x01) {
                if (pending)
                    pen.quad_to(off.x, off.y, pts[i].x, pts[i].y);
                else
                    pen.line_to(pts[i].x, pts[i].y);
                pending = false;
            } else {
                if (pending)
                    pen.quad_to(off.x, off.y, 0.5 * (off.x + pts[i].x), 0.5 * (off.y + pts[i].y));
                off = pts[i];
                pending = true;
            }
        }
    }
    return true;
}

// --- Outlines: CFF -------------------------------------------------------------------

struct OpenTypeFont::CffState {
    BoundsPen* pen = nullptr;
    // The local subroutines of the glyph's Private DICT.
    const std::vector<uint32_t>* local = nullptr;
    double stack[CFF_STACK_LIMIT] = {};
    double transient[CFF_TRANSIENT_SIZE] = {};
    int depth = 0;
    double x = 0, y = 0;
    int hints = 0;
    int mask_bytes = 0;
    bool width_seen = false;
    bool moved = false;
    bool ended = false;
    int budget = CFF_WORK_BUDGET;
    // For the strict pass: the farthest any point, control points included,
    // lies from the origin on either axis, and whether a subroutine was
    // called by a number that is not whole.
    bool strict = false;
    bool fractional_call = false;
    double reach = 0;
    // False once the pen has been sent to a point that is not a number: the
    // arithmetic operators work in doubles and can multiply their way to an
    // infinity, and from two of those to a NaN, which no comparison bounds.
    bool finite = true;

    void note(double px, double py)
    {
        reach = std::max(reach, std::max(std::fabs(px), std::fabs(py)));
    }

    // After the pen moves. A control point that is not finite leaves the
    // point after it not finite too, so the pen's own position speaks for
    // the whole segment.
    void check_finite()
    {
        if (!std::isfinite(x) || !std::isfinite(y))
            finite = false;
    }

    // The first stack-clearing operator may carry the glyph's width in front
    // of its own operands; `odd_means_width` says which parity gives it away.
    // Returns the index of the operator's first real operand.
    int take_width(bool odd_means_width)
    {
        if (width_seen)
            return 0;
        width_seen = true;
        const bool odd = (depth % 2) != 0;
        return odd == odd_means_width && depth > 0 ? 1 : 0;
    }

    void move(double dx, double dy)
    {
        x += dx;
        y += dy;
        check_finite();
        pen->move_to(x, y);
        moved = true;
        --budget;
        if (strict)
            note(x, y);
    }

    // A drawing operator before any moveto starts the path where the pen is.
    void begin()
    {
        if (!moved)
            move(0, 0);
    }

    void line(double dx, double dy)
    {
        x += dx;
        y += dy;
        check_finite();
        pen->line_to(x, y);
        --budget;
        if (strict)
            note(x, y);
    }

    void curve(double dx1, double dy1, double dx2, double dy2, double dx3, double dy3)
    {
        const double c1x = x + dx1, c1y = y + dy1;
        const double c2x = c1x + dx2, c2y = c1y + dy2;
        x = c2x + dx3;
        y = c2y + dy3;
        check_finite();
        pen->curve_to(c1x, c1y, c2x, c2y, x, y);
        --budget;
        if (strict) {
            note(c1x, c1y);
            note(c2x, c2y);
            note(x, y);
        }
    }
};

// False if the charstring is malformed or uses something this reader does not
// read; the glyph then has no box. A charstring whose last operator sends the
// pen somewhere that is not a number returns true with `s.finite` false: the
// caller looks at both.
bool OpenTypeFont::run_charstring(const uint8_t* code, size_t size, CffState& s, int depth) const
{
    size_t i = 0;
    double* st = s.stack;
    while (i < size) {
        const unsigned b = code[i];
        if (b >= 32 || b == 28) {
            double value;
            if (b == 28) {
                if (size - i < 3)
                    return false;
                value = static_cast<int16_t>((code[i + 1] << 8) | code[i + 2]);
                i += 3;
            } else if (b <= 246) {
                value = static_cast<int>(b) - 139;
                i += 1;
            } else if (b <= 254) {
                if (size - i < 2)
                    return false;
                const int w = code[i + 1];
                value = b <= 250 ? (static_cast<int>(b) - 247) * 256 + w + 108
                                 : -(static_cast<int>(b) - 251) * 256 - w - 108;
                i += 2;
            } else {
                if (size - i < 5)
                    return false;
                const int32_t fixed = static_cast<int32_t>(
                    (uint32_t(code[i + 1]) << 24) | (uint32_t(code[i + 2]) << 16) |
                    (uint32_t(code[i + 3]) << 8) | uint32_t(code[i + 4]));
                value = fixed / 65536.0;
                i += 5;
            }
            if (s.depth == CFF_STACK_LIMIT)
                return false;
            st[s.depth++] = value;
            continue;
        }
        ++i;
        if (--s.budget < 0 || !s.finite)
            return false;
        const int n = s.depth;
        switch (b) {
        case 1:    // hstem
        case 3:    // vstem
        case 18:   // hstemhm
        case 23: { // vstemhm
            const int k = s.take_width(true);
            s.hints += (n - k) / 2;
            s.depth = 0;
            break;
        }
        case 19:   // hintmask
        case 20: { // cntrmask
            if (s.mask_bytes == 0) {
                // Operands left on the stack are the vertical stems the first
                // mask implies.
                const int k = s.take_width(true);
                s.hints += (n - k) / 2;
                s.mask_bytes = (s.hints + 7) / 8;
            }
            s.depth = 0;
            if (size - i < static_cast<size_t>(s.mask_bytes))
                return false;
            i += static_cast<size_t>(s.mask_bytes);
            break;
        }
        case 21: { // rmoveto
            const int k = s.take_width(true);
            if (n - k < 2)
                return false;
            s.move(st[k], st[k + 1]);
            s.depth = 0;
            break;
        }
        case 22:  // hmoveto
        case 4: { // vmoveto
            const int k = s.take_width(false);
            if (n - k < 1)
                return false;
            if (b == 22)
                s.move(st[k], 0);
            else
                s.move(0, st[k]);
            s.depth = 0;
            break;
        }
        case 5: // rlineto
            s.begin();
            for (int k = 0; k + 1 < n; k += 2)
                s.line(st[k], st[k + 1]);
            s.depth = 0;
            break;
        case 6:   // hlineto
        case 7: { // vlineto
            s.begin();
            bool horizontal = b == 6;
            for (int k = 0; k < n; ++k) {
                if (horizontal)
                    s.line(st[k], 0);
                else
                    s.line(0, st[k]);
                horizontal = !horizontal;
            }
            s.depth = 0;
            break;
        }
        case 8: // rrcurveto
            s.begin();
            for (int k = 0; k + 5 < n; k += 6)
                s.curve(st[k], st[k + 1], st[k + 2], st[k + 3], st[k + 4], st[k + 5]);
            s.depth = 0;
            break;
        case 24: { // rcurveline
            s.begin();
            int k = 0;
            for (; k + 7 < n; k += 6)
                s.curve(st[k], st[k + 1], st[k + 2], st[k + 3], st[k + 4], st[k + 5]);
            if (k + 1 < n)
                s.line(st[k], st[k + 1]);
            s.depth = 0;
            break;
        }
        case 25: { // rlinecurve
            s.begin();
            int k = 0;
            for (; k + 7 < n; k += 2)
                s.line(st[k], st[k + 1]);
            if (k + 5 < n)
                s.curve(st[k], st[k + 1], st[k + 2], st[k + 3], st[k + 4], st[k + 5]);
            s.depth = 0;
            break;
        }
        case 26: { // vvcurveto
            s.begin();
            int k = 0;
            double dx1 = 0;
            if (n % 2 != 0)
                dx1 = st[k++];
            for (; k + 3 < n; k += 4) {
                s.curve(dx1, st[k], st[k + 1], st[k + 2], 0, st[k + 3]);
                dx1 = 0;
            }
            s.depth = 0;
            break;
        }
        case 27: { // hhcurveto
            s.begin();
            int k = 0;
            double dy1 = 0;
            if (n % 2 != 0)
                dy1 = st[k++];
            for (; k + 3 < n; k += 4) {
                s.curve(st[k], dy1, st[k + 1], st[k + 2], st[k + 3], 0);
                dy1 = 0;
            }
            s.depth = 0;
            break;
        }
        case 30:   // vhcurveto
        case 31: { // hvcurveto
            s.begin();
            bool horizontal = b == 31;
            int k = 0;
            while (n - k >= 4) {
                const double a = st[k], c1 = st[k + 1], c2 = st[k + 2], d = st[k + 3];
                k += 4;
                // The last curve may carry a fifth operand, the end point's
                // offset along the axis that would otherwise be zero.
                const bool last = n - k == 1;
                const double extra = last ? st[k] : 0.0;
                if (horizontal)
                    s.curve(a, 0, c1, c2, extra, d);
                else
                    s.curve(0, a, c1, c2, d, extra);
                if (last)
                    ++k;
                horizontal = !horizontal;
            }
            s.depth = 0;
            break;
        }
        case 10:   // callsubr
        case 29: { // callgsubr
            const std::vector<uint32_t>& subrs = b == 10 ? *s.local : _global_subr_offsets;
            const size_t count = subrs.empty() ? 0 : subrs.size() - 1;
            if (n < 1 || depth >= CFF_CALL_DEPTH)
                return false;
            const double index = st[--s.depth] + subr_bias(count);
            if (!(index >= 0) || index >= static_cast<double>(count))
                return false;
            if (index != std::floor(index))
                s.fractional_call = true;
            const size_t which = static_cast<size_t>(index);
            if (!run_charstring(_cff.data + subrs[which], subrs[which + 1] - subrs[which], s,
                                depth + 1))
                return false;
            if (s.ended)
                return true;
            break;
        }
        case 11: // return
            return true;
        case 14: // endchar
            s.take_width(true);
            s.ended = true;
            return true;
        case 12: {
            if (i >= size)
                return false;
            const unsigned op = code[i++];
            if (op >= 34 && op <= 37) {
                // The flex operators: two curves, and the stack is cleared.
                // hflex1 and flex1 follow fontTools; see the UPSTREAM NOTICE
                // above BoundsPen.
                s.begin();
                if (op == 35 && n >= 12) { // flex
                    s.curve(st[0], st[1], st[2], st[3], st[4], st[5]);
                    s.curve(st[6], st[7], st[8], st[9], st[10], st[11]);
                } else if (op == 34 && n >= 7) { // hflex
                    s.curve(st[0], 0, st[1], st[2], st[3], 0);
                    s.curve(st[4], 0, st[5], -st[2], st[6], 0);
                } else if (op == 36 && n >= 9) { // hflex1
                    const double dy6 = -(st[1] + st[3] + 0.0 + 0.0 + st[7]);
                    s.curve(st[0], st[1], st[2], st[3], st[4], 0);
                    s.curve(st[5], 0, st[6], st[7], st[8], dy6);
                } else if (op == 37 && n >= 11) { // flex1
                    const double dx = st[0] + st[2] + st[4] + st[6] + st[8];
                    const double dy = st[1] + st[3] + st[5] + st[7] + st[9];
                    const bool wide = std::fabs(dx) > std::fabs(dy);
                    s.curve(st[0], st[1], st[2], st[3], st[4], st[5]);
                    s.curve(st[6], st[7], st[8], st[9], wide ? st[10] : -dx, wide ? -dy : st[10]);
                } else {
                    return false;
                }
                s.depth = 0;
                break;
            }
            // The arithmetic, storage and conditional operators (Type 2
            // specification, section 4.5): they take their operands off the
            // top of the stack, leave a result there, and touch nothing below.
            // `a` is the top operand and `b` the one under it.
            const double a = n >= 1 ? st[n - 1] : 0.0, b2 = n >= 2 ? st[n - 2] : 0.0;
            const auto unary = [&](double result) {
                st[n - 1] = result;
                return true;
            };
            const auto binary = [&](double result) {
                st[n - 2] = result;
                s.depth = n - 1;
                return true;
            };
            // An index into the transient array, if `a` is one.
            const auto slot = [&]() {
                return a >= 0 && a < CFF_TRANSIENT_SIZE && a == std::floor(a) ? static_cast<int>(a)
                                                                              : -1;
            };
            bool done = false;
            switch (op) {
            case 3: // and
                done = n >= 2 && binary(b2 != 0 && a != 0 ? 1.0 : 0.0);
                break;
            case 4: // or
                done = n >= 2 && binary(b2 != 0 || a != 0 ? 1.0 : 0.0);
                break;
            case 5: // not
                done = n >= 1 && unary(a == 0 ? 1.0 : 0.0);
                break;
            case 9: // abs
                done = n >= 1 && unary(std::fabs(a));
                break;
            case 10: // add
                done = n >= 2 && binary(b2 + a);
                break;
            case 11: // sub
                done = n >= 2 && binary(b2 - a);
                break;
            case 12: // div
                done = n >= 2 && a != 0 && binary(b2 / a);
                break;
            case 14: // neg
                done = n >= 1 && unary(-a);
                break;
            case 15: // eq
                done = n >= 2 && binary(b2 == a ? 1.0 : 0.0);
                break;
            case 18: // drop
                if (n >= 1) {
                    s.depth = n - 1;
                    done = true;
                }
                break;
            case 20: // put: value, index
                if (n >= 2 && slot() >= 0) {
                    s.transient[slot()] = b2;
                    s.depth = n - 2;
                    done = true;
                }
                break;
            case 21: // get: index
                done = n >= 1 && slot() >= 0 && unary(s.transient[slot()]);
                break;
            case 22: // ifelse: s1 s2 v1 v2 leaves s1 if v1 <= v2, else s2
                if (n >= 4) {
                    st[n - 4] = b2 <= a ? st[n - 4] : st[n - 3];
                    s.depth = n - 3;
                    done = true;
                }
                break;
            case 24: // mul
                done = n >= 2 && binary(b2 * a);
                break;
            case 26: // sqrt
                done = n >= 1 && a >= 0 && unary(std::sqrt(a));
                break;
            case 27: // dup
                if (n >= 1 && n < CFF_STACK_LIMIT) {
                    st[n] = a;
                    s.depth = n + 1;
                    done = true;
                }
                break;
            case 28: // exch
                if (n >= 2) {
                    st[n - 2] = a;
                    st[n - 1] = b2;
                    done = true;
                }
                break;
            case 29: { // index: a copy of the element that many below the top
                const double below = a >= 0 ? std::floor(a) : 0.0;
                if (n >= 2 && below <= n - 2)
                    done = unary(st[n - 2 - static_cast<int>(below)]);
                break;
            }
            case 30: { // roll: count, shift; a positive shift moves the top down
                if (n < 2 || !(b2 >= 0) || b2 > n - 2 || b2 != std::floor(b2) ||
                    a != std::floor(a) || std::fabs(a) > 1e9)
                    break;
                const int count = static_cast<int>(b2);
                s.depth = n - 2;
                if (count > 0) {
                    const int shift =
                        ((static_cast<int>(a) % count) + count) % count; // 0 .. count - 1
                    double* first = st + (s.depth - count);
                    std::rotate(first, first + (count - shift), first + count);
                }
                done = true;
                break;
            }
            default: // 23 is random, which has no one answer; the rest are reserved
                break;
            }
            if (!done) {
                warn("a CFF charstring uses an operator that is not read (random, a reserved "
                     "one) or misuses one; that glyph has no ink box");
                return false;
            }
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

bool OpenTypeFont::draw_cff(uint32_t glyph, BoundsPen& pen) const
{
    if (size_t(glyph) + 1 >= _charstring_offsets.size())
        return false;
    CffState state;
    state.pen = &pen;
    state.local = local_subrs_of(glyph);
    // A glyph of a CID-keyed font that FDSelect gives no Font DICT, or one
    // the font does not have.
    if (state.local == nullptr)
        return false;
    const uint32_t begin = _charstring_offsets[glyph], end = _charstring_offsets[glyph + 1];
    // A glyph with a point that is not a number has no box: layout
    // multiplies what this answers.
    return run_charstring(_cff.data + begin, end - begin, state, 0) && state.finite;
}

GlyphBounds OpenTypeFont::bounds(uint32_t glyph) const
{
    if (!_loaded || glyph >= _glyph_count)
        return GlyphBounds{};
    if (_bounds_known[glyph])
        return _bounds[glyph];
    BoundsPen pen;
    bool ok;
    if (_is_cff) {
        ok = draw_cff(glyph, pen);
    } else {
        int budget = GLYF_WORK_BUDGET;
        ok = draw_glyf(glyph, pen, nullptr, 0, true, budget);
    }
    GlyphBounds result;
    if (!ok)
        warn("a glyph's outline is malformed or unsupported; its ink box is empty");
    else if (pen.any)
        result = GlyphBounds{pen.x_min, pen.y_min, pen.x_max, pen.y_max};
    _bounds[glyph] = result;
    _bounds_known[glyph] = 1;
    return result;
}

// --- The strict pass -------------------------------------------------------------------

bool OpenTypeFont::strict_check(std::string* why) const
{
    if (_strict_state == 0) {
        _box_repairs.clear();
        const char* reason = !_loaded ? "no font is loaded" : strict_reason();
        _strict_state = reason == nullptr ? 1 : -1;
        _strict_why = reason != nullptr ? reason : "";
        if (reason != nullptr)
            _box_repairs.clear();
    }
    if (_strict_state < 0 && why != nullptr)
        *why = _strict_why;
    return _strict_state > 0;
}

const std::vector<GlyphBoxRepair>& OpenTypeFont::atlas_box_repairs() const
{
    strict_check();
    return _box_repairs;
}

// Null if the file passes, else what is wrong with it.
const char* OpenTypeFont::strict_reason() const
{
    // The container. Tables that share nothing cannot add up to more than the
    // file; ones that do can make a copy of the file many times its size.
    if (_table_bytes > _size)
        return "the font's tables overlap";
    const bool otto = _sfnt_version == tag_of("OTTO");
    if (otto != _is_cff)
        return "the file's version tag and its outline tables disagree";
    if (!_is_cff && (table("CFF ").present() || table("CFF2").present()))
        return "the font has both TrueType and CFF outlines";

    bool bad = false;
    const Table head = table("head"), hhea = table("hhea"), hmtx = table("hmtx");
    const Reader h{head.data, head.size, &bad};
    if (head.size < 54 || hhea.size < 36)
        return "head or hhea is shorter than its format";
    if (_units_per_em < 16 || _units_per_em > 16384)
        return "head.unitsPerEm is outside 16..16384";
    const unsigned loca_format = h.u16(50);
    if (!_is_cff && loca_format > 1)
        return "head.indexToLocFormat is neither 0 nor 1";
    const int extent = _ascender - _descender;
    if (extent <= 0)
        return "hhea's ascender is not above its descender";
    // One full record per glyph up to numberOfHMetrics, then a side bearing
    // for each glyph after.
    const size_t long_count = std::min(_hmetric_count, _glyph_count);
    if (_hmetric_count == 0)
        return "hhea.numberOfHMetrics is zero";
    if (hmtx.size < long_count * 4 + (size_t(_glyph_count) - long_count) * 2)
        return "the hmtx table is shorter than the glyph count needs";
    if (_loca_unterminated)
        return "the loca table has no entry after the last glyph's";

    const double reach = STRICT_REACH * extent;
    return _is_cff ? strict_cff(reach) : strict_glyf(reach);
}

struct OpenTypeFont::StrictGlyph {
    uint8_t state = 0;       // 0 not visited, 1 being visited, 2 done
    uint32_t points = 0;     // with every component expanded
    uint32_t components = 0; // likewise
    double reach_x = 0, reach_y = 0;
    // Whether the glyph has any point that an edge runs to, and if so a box
    // that holds them all, control points included, with every component
    // expanded: exact for a simple glyph, an outer bound for a composite.
    bool inked = false;
    double x_lo = 0, x_hi = 0, y_lo = 0, y_hi = 0;

    void hold(double xa, double xb, double ya, double yb)
    {
        if (!inked) {
            inked = true;
            x_lo = xa, x_hi = xb, y_lo = ya, y_hi = yb;
            return;
        }
        x_lo = std::min(x_lo, xa), x_hi = std::max(x_hi, xb);
        y_lo = std::min(y_lo, ya), y_hi = std::max(y_hi, yb);
    }
};

const char* OpenTypeFont::strict_glyf(double reach) const
{
    // Every glyph's data lies in glyf, in order.
    for (size_t g = 0; g < _glyph_count; ++g)
        if (_loca[g] > _loca[g + 1])
            return "the loca table's offsets are not in order";
    if (_loca[_glyph_count] > _glyf.size)
        return "the loca table points past the end of glyf";
    std::vector<StrictGlyph> facts(_glyph_count);
    for (uint32_t g = 0; g < _glyph_count; ++g) {
        if (const char* reason = strict_glyf_glyph(g, facts, 0, reach))
            return reason;
    }
    return nullptr;
}

// Everything a rasteriser reads of one glyph, checked against the glyph's own
// extent in glyf; for a composite, its components first.
const char* OpenTypeFont::strict_glyf_glyph(uint32_t glyph, std::vector<StrictGlyph>& facts,
                                            int depth, double reach) const
{
    StrictGlyph& fact = facts[glyph];
    if (fact.state == 2)
        return nullptr;
    if (fact.state == 1)
        return "a composite glyph contains itself";
    if (depth > STRICT_COMPONENT_DEPTH)
        return "composite glyphs are nested too deeply";
    const size_t begin = _loca[glyph], length = _loca[glyph + 1] - begin;
    if (length == 0) {
        fact.state = 2;
        return nullptr;
    }
    if (length < 10)
        return "a glyph is shorter than its header";
    bool bad = false;
    const Reader g{_glyf.data + begin, length, &bad};
    const int contours = g.s16(0);
    const int x_min = g.s16(2), y_min = g.s16(4), x_max = g.s16(6), y_max = g.s16(8);
    if (contours < -1)
        return "a glyph has a negative contour count that is not a composite's";
    if (x_min > x_max || y_min > y_max)
        return "a glyph's bounding box is inside out";
    if (-x_min > reach || x_max > reach || -y_min > reach || y_max > reach)
        return "a glyph's bounding box is far outside the font's height";

    if (contours >= 0) {
        const size_t n_contours = static_cast<size_t>(contours);
        size_t n_points = 0;
        if (n_contours > 0) {
            if (!g.has(10, n_contours * 2 + 2))
                return "a glyph's contour end points run past its data";
            for (size_t i = 0; i < n_contours; ++i) {
                const size_t last = g.u16(10 + i * 2);
                if (last + 1 <= n_points)
                    return "a glyph's contour end points are not increasing";
                n_points = last + 1;
            }
            size_t at = 10 + n_contours * 2;
            at += 2 + g.u16(at);
            // The flags, a run at a time, counting the coordinate bytes they
            // call for.
            size_t x_bytes = 0, y_bytes = 0;
            const size_t flags_at = at;
            for (size_t i = 0; i < n_points;) {
                if (!g.has(at, 1))
                    return "a glyph's flags run past its data";
                const unsigned flag = g.p[at++];
                size_t run = 1;
                if (flag & 0x08) {
                    if (!g.has(at, 1))
                        return "a glyph's flags run past its data";
                    run += g.p[at++];
                }
                if (run > n_points - i)
                    return "a glyph's flags repeat past its last point";
                x_bytes += run * ((flag & 0x02) ? 1u : (flag & 0x10) ? 0u : 2u);
                y_bytes += run * ((flag & 0x04) ? 1u : (flag & 0x20) ? 0u : 2u);
                i += run;
            }
            if (!g.has(at, x_bytes + y_bytes))
                return "a glyph's coordinates run past its data";
            // The coordinates themselves, for how far the outline reaches.
            size_t flag_at = flags_at, x_at = at, y_at = at + x_bytes;
            long long x = 0, y = 0, far_x = 0, far_y = 0;
            size_t contour = 0, contour_first = 0;
            for (size_t i = 0; i < n_points;) {
                const unsigned flag = g.p[flag_at++];
                size_t run = 1;
                if (flag & 0x08)
                    run += g.p[flag_at++];
                for (size_t k = 0; k < run; ++k) {
                    // stb_truetype reads the point after a contour's first
                    // when that first is off the curve. Every point has one
                    // after it in the glyph but the last, so the one contour
                    // that cannot be read is a last contour of a single
                    // off-curve point.
                    const size_t point = i + k, contour_last = g.u16(10 + contour * 2);
                    if (point == contour_first && point + 1 == n_points && !(flag & 0x01))
                        return "a glyph ends in a contour of one off-curve point";
                    // A contour of one point on the curve has no edge: fonts
                    // mark an attachment point with one, anywhere.
                    const bool lone =
                        point == contour_first && point == contour_last && (flag & 0x01);
                    if (point == contour_last) {
                        ++contour;
                        contour_first = point + 1;
                    }
                    if (flag & 0x02) {
                        const int d = g.p[x_at++];
                        x += (flag & 0x10) ? d : -d;
                    } else if (!(flag & 0x10)) {
                        x += static_cast<int16_t>((g.p[x_at] << 8) | g.p[x_at + 1]);
                        x_at += 2;
                    }
                    if (flag & 0x04) {
                        const int d = g.p[y_at++];
                        y += (flag & 0x20) ? d : -d;
                    } else if (!(flag & 0x20)) {
                        y += static_cast<int16_t>((g.p[y_at] << 8) | g.p[y_at + 1]);
                        y_at += 2;
                    }
                    far_x = std::max(far_x, x < 0 ? -x : x);
                    far_y = std::max(far_y, y < 0 ? -y : y);
                    if (!lone)
                        fact.hold(static_cast<double>(x), static_cast<double>(x),
                                  static_cast<double>(y), static_cast<double>(y));
                }
                i += run;
            }
            // Past 16 bits the rasterisers no longer agree on where a point is.
            if (far_x > 32767 || far_y > 32767)
                return "a glyph's coordinates do not fit 16 bits";
            fact.reach_x = static_cast<double>(far_x);
            fact.reach_y = static_cast<double>(far_y);
        }
        fact.points = static_cast<uint32_t>(n_points);
    } else {
        fact.state = 1;
        uint64_t points = 0, components = 0;
        double far_x = 0, far_y = 0;
        size_t at = 10;
        for (;;) {
            if (!g.has(at, 4))
                return "a composite glyph's components run past its data";
            const unsigned flags = g.u16(at);
            const uint32_t child = g.u16(at + 2);
            at += 4;
            const size_t arguments = (flags & 0x0001) ? 4 : 2;
            const size_t transform = (flags & 0x0008)   ? 2
                                     : (flags & 0x0040) ? 4
                                     : (flags & 0x0080) ? 8
                                                        : 0;
            if (!g.has(at, arguments + transform))
                return "a composite glyph's components run past its data";
            if (!(flags & 0x0002))
                return "a composite glyph places a component by point matching";
            if (child >= _glyph_count)
                return "a composite glyph names a component the font does not have";
            const double dx = (flags & 0x0001) ? g.s16(at) : static_cast<int8_t>(g.p[at]);
            const double dy = (flags & 0x0001) ? g.s16(at + 2) : static_cast<int8_t>(g.p[at + 1]);
            at += arguments;
            double xx = 1, xy = 0, yx = 0, yy = 1;
            if (flags & 0x0008) {
                xx = yy = g.s16(at) / 16384.0;
            } else if (flags & 0x0040) {
                xx = g.s16(at) / 16384.0;
                yy = g.s16(at + 2) / 16384.0;
            } else if (flags & 0x0080) {
                xx = g.s16(at) / 16384.0;
                xy = g.s16(at + 2) / 16384.0;
                yx = g.s16(at + 4) / 16384.0;
                yy = g.s16(at + 6) / 16384.0;
            }
            at += transform;
            if (const char* reason = strict_glyf_glyph(child, facts, depth + 1, reach))
                return reason;
            const StrictGlyph& part = facts[child];
            points += part.points;
            components += 1 + uint64_t(part.components);
            if (points > STRICT_COMPOSITE_POINTS || components > STRICT_COMPOSITE_COMPONENTS)
                return "a composite glyph expands to more than a glyph can hold";
            // An upper bound on where the component's points land, whichever
            // way a rasteriser applies the transform: stb_truetype scales the
            // result again by the length of each matrix row.
            const double stretch =
                std::max(1.0, std::max(std::sqrt(xx * xx + xy * xy), std::sqrt(yx * yx + yy * yy)));
            far_x = std::max(far_x, stretch * (std::fabs(xx) * part.reach_x +
                                               std::fabs(yx) * part.reach_y + std::fabs(dx)));
            far_y = std::max(far_y, stretch * (std::fabs(xy) * part.reach_x +
                                               std::fabs(yy) * part.reach_y + std::fabs(dy)));
            if (part.inked) {
                // Where the component's box lands: x' = xx x + yx y + dx and
                // y' = xy x + yy y + dy, each term over its interval. The
                // second scaling stb_truetype applies is by a length, which
                // moves no point across an axis.
                const auto span = [](double k, double lo, double hi, double* a, double* b) {
                    *a += std::min(k * lo, k * hi);
                    *b += std::max(k * lo, k * hi);
                };
                double xa = dx, xb = dx, ya = dy, yb = dy;
                span(xx, part.x_lo, part.x_hi, &xa, &xb);
                span(yx, part.y_lo, part.y_hi, &xa, &xb);
                span(xy, part.x_lo, part.x_hi, &ya, &yb);
                span(yy, part.y_lo, part.y_hi, &ya, &yb);
                fact.hold(xa, xb, ya, yb);
            }
            if (!(flags & 0x0020))
                break;
        }
        fact.points = static_cast<uint32_t>(points);
        fact.components = static_cast<uint32_t>(components);
        fact.reach_x = far_x;
        fact.reach_y = far_y;
    }
    if (fact.reach_x > reach || fact.reach_y > reach)
        return "a glyph's outline is far outside the font's height";
    // stb_truetype sizes a glyph's bitmap by this header's box and then
    // rasterises the outline into it. Where the outline leaves the box it is
    // clipped, with one exception: when the box's top is at or below the
    // baseline the bitmap's first row can be row 0, and an edge lying wholly
    // above row 0 then fails an assertion in its rasteriser (the test of
    // `z->ey >= scan_y_top` in stbtt__rasterize_sorted_edges), which is the
    // host's IM_ASSERT. A glyph with such a box and a point of an edge above
    // the baseline is noted for build_atlas_font to raise its yMax over its
    // points. For a composite the points' box is an outer bound, which
    // makes a bitmap larger than it need be. Where no 16-bit yMax holds the
    // points, which only a composite's bound can come to, the font is refused.
    if (y_max <= 0 && fact.inked && fact.y_hi > 0) {
        const double top = std::ceil(fact.y_hi);
        if (!(top <= 32767.0))
            return "a glyph is drawn above the baseline under a bounding box that ends at or "
                   "below it, further up than a bounding box can say";
        _box_repairs.push_back(GlyphBoxRepair{glyph, begin, static_cast<int16_t>(top)});
    }
    fact.state = 2;
    return nullptr;
}

const char* OpenTypeFont::strict_cff(double reach) const
{
    if (_cff_tolerated != nullptr)
        return _cff_tolerated;
    if (_charstring_offsets.size() < size_t(_glyph_count) + 1)
        return "the CFF table has fewer charstrings than maxp has glyphs";
    // A charstring that is malformed, or uses an operator this reader stops
    // at, draws nothing here and stops a rasteriser where it stops this
    // reader; that is not refused. What is: one that costs more than a glyph
    // can, calls a subroutine by a fraction, computes a coordinate that is
    // not a number, or draws far outside the font.
    for (uint32_t g = 0; g < _glyph_count; ++g) {
        BoundsPen pen;
        CffState state;
        state.pen = &pen;
        state.strict = true;
        state.budget = STRICT_CFF_WORK;
        state.local = local_subrs_of(g);
        // A rasteriser looks the glyph's Font DICT up and does not check
        // that there is one.
        if (state.local == nullptr)
            return "a glyph of the CID-keyed CFF font has no Font DICT, or one the font does "
                   "not have";
        const uint32_t begin = _charstring_offsets[g], end = _charstring_offsets[g + 1];
        run_charstring(_cff.data + begin, end - begin, state, 0);
        if (state.budget < 0)
            return "a CFF charstring does more work than a glyph can need";
        if (state.fractional_call)
            return "a CFF charstring calls a subroutine by a number that is not whole";
        if (!state.finite)
            return "a CFF charstring's arithmetic puts a point where no number is";
        if (!(state.reach <= reach))
            return "a glyph's outline is far outside the font's height";
    }
    return nullptr;
}

// --- GPOS pair kerning ---------------------------------------------------------------

void OpenTypeFont::read_gpos()
{
    const Table gpos = table("GPOS");
    if (!gpos.present())
        return;
    bool bad = false;
    const Reader r{gpos.data, gpos.size, &bad};
    const size_t feature_list = r.u16(6), lookup_list = r.u16(8);
    if (bad || feature_list == 0 || lookup_list == 0)
        return;

    // The lookups of every feature record tagged 'kern', in index order, each
    // once. Feature tables may overlap, so the indices read are counted
    // against the table's size: tables that share nothing cannot list more
    // indices than the table has byte pairs.
    std::vector<uint16_t> lookups;
    {
        std::vector<uint8_t> named(65536, 0);
        size_t work = gpos.size / 2 + 1;
        const Reader f = r.from(feature_list);
        const size_t count = f.u16(0);
        for (size_t i = 0; i < count && !bad; ++i) {
            if (f.u32(2 + i * 6) != tag_of("kern"))
                continue;
            const Reader feature = f.from(f.u16(6 + i * 6));
            const size_t n = feature.u16(2);
            if (n > work)
                bad = true;
            for (size_t k = 0; k < n && !bad; ++k)
                named[feature.u16(4 + k * 2)] = 1;
            work -= std::min(n, work);
        }
        if (bad) {
            warn("the GPOS feature list is malformed; the font has no kerning");
            return;
        }
        for (uint32_t index = 0; index < named.size(); ++index)
            if (named[index])
                lookups.push_back(static_cast<uint16_t>(index));
    }
    if (lookups.size() > MAX_KERN_LOOKUPS) {
        lookups.resize(MAX_KERN_LOOKUPS);
        warn("the GPOS 'kern' features name more lookups than are read; the rest were ignored");
    }

    const Reader list = r.from(lookup_list);
    const size_t lookup_count = list.u16(0);
    std::vector<uint16_t> coverage;
    // Where each lookup and each pair subtable already read starts in the
    // table. One that is named again would be read into an identical copy,
    // and a copy never answers: the first subtable with a value does.
    std::vector<uint32_t> lookups_read, subtables_read;
    const auto first_visit = [](std::vector<uint32_t>& sorted, uint32_t offset) {
        const auto it = std::lower_bound(sorted.begin(), sorted.end(), offset);
        if (it != sorted.end() && *it == offset)
            return false;
        sorted.insert(it, offset);
        return true;
    };
    // What may still be examined and kept; see the limits at the top.
    size_t offsets_left = gpos.size / 2 + 1;
    size_t units_left = gpos.size * KERN_UNITS_PER_BYTE + KERN_UNITS_FLOOR;
    size_t class_bytes_left = KERN_CLASS_BYTES;
    bool exhausted = false;

    for (const uint16_t index : lookups) {
        if (exhausted)
            break;
        if (index >= lookup_count)
            continue;
        bad = false;
        // An offset of zero is OpenType's "no table", here and below: what it
        // would name is skipped, not read from the start of its parent.
        const size_t lookup_at = list.u16(2 + size_t(index) * 2);
        if (lookup_at == 0)
            continue;
        const Reader lookup = list.from(lookup_at);
        const unsigned lookup_type = lookup.u16(0);
        const size_t sub_count = lookup.u16(4);
        if (bad || lookup.p == nullptr) {
            warn("a GPOS lookup is malformed and was ignored");
            continue;
        }
        if (!first_visit(lookups_read, static_cast<uint32_t>(lookup.p - gpos.data)))
            continue;
        for (size_t si = 0; si < sub_count && !bad && !exhausted; ++si) {
            if (_kern.size() >= MAX_KERN_SUBTABLES || offsets_left == 0) {
                exhausted = true;
                break;
            }
            --offsets_left;
            bool sub_bad = false;
            const size_t sub_at = lookup.u16(6 + si * 2);
            Reader sub = lookup.from(sub_at);
            if (bad)
                break;
            if (sub_at == 0)
                continue;
            sub.bad = &sub_bad;
            unsigned type = lookup_type;
            if (type == 9) {
                type = sub.u16(2);
                const size_t extension_at = sub.u32(4);
                if (extension_at == 0)
                    continue;
                sub = sub.from(extension_at);
            }
            if (type != 2 || sub_bad || sub.p == nullptr)
                continue;
            if (!first_visit(subtables_read, static_cast<uint32_t>(sub.p - gpos.data)))
                continue;

            const unsigned format = sub.u16(0);
            const unsigned vf1 = sub.u16(4), vf2 = sub.u16(6);
            const size_t size1 = size_t(popcount16(vf1)) * 2, size2 = size_t(popcount16(vf2)) * 2;
            // Where XAdvance sits in the first value record, after XPlacement
            // and YPlacement if those are present.
            const bool has_advance = (vf1 & 0x0004) != 0;
            const size_t advance_at = size_t(popcount16(vf1 & 0x0003)) * 2;
            KernSubtable out;
            out.lookup = index;
            const size_t coverage_at = sub.u16(2);
            if (coverage_at == 0 || !read_coverage(sub.from(coverage_at), coverage) || sub_bad) {
                warn("a GPOS pair subtable is malformed and was ignored");
                continue;
            }
            if (format == 1) {
                out.format = 1;
                const size_t sets = std::min<size_t>(sub.u16(8), coverage.size());
                const size_t record = 2 + size1 + size2;
                struct Pair {
                    uint32_t key;
                    int16_t value;
                };
                std::vector<Pair> pairs;
                for (size_t i = 0; i < sets && !sub_bad; ++i) {
                    const size_t set_at = sub.u16(10 + i * 2);
                    if (set_at == 0)
                        continue;
                    const Reader set = sub.from(set_at);
                    const size_t n = set.u16(0);
                    if (!set.need(2, n * record)) {
                        sub_bad = true;
                        break;
                    }
                    if (n > units_left) {
                        exhausted = true;
                        break;
                    }
                    units_left -= n;
                    for (size_t k = 0; k < n; ++k) {
                        const size_t at = 2 + k * record;
                        const int16_t value =
                            has_advance ? set.s16(at + 2 + advance_at) : int16_t(0);
                        pairs.push_back({(uint32_t(coverage[i]) << 16) | set.u16(at), value});
                    }
                }
                if (exhausted)
                    break;
                if (sub_bad) {
                    warn("a GPOS pair subtable is malformed and was ignored");
                    continue;
                }
                // A pair listed twice keeps its last value, as a dict built in
                // file order does.
                std::stable_sort(pairs.begin(), pairs.end(),
                                 [](const Pair& a, const Pair& b) { return a.key < b.key; });
                for (size_t i = 0; i < pairs.size(); ++i) {
                    if (i + 1 < pairs.size() && pairs[i + 1].key == pairs[i].key)
                        continue;
                    out.pair_keys.push_back(pairs[i].key);
                    out.pair_values.push_back(pairs[i].value);
                }
            } else if (format == 2) {
                out.format = 2;
                out.class1_count = sub.u16(12);
                out.class2_count = sub.u16(14);
                const size_t class_bytes = size_t(_glyph_count) * 5;
                if (class_bytes > class_bytes_left) {
                    exhausted = true;
                    break;
                }
                // In 64 bits whatever size_t is: the counts and the record
                // size multiply to more than 32 bits can hold.
                const uint64_t record = size1 + size2;
                const uint64_t cells = uint64_t(out.class1_count) * out.class2_count;
                // No ClassDef puts every glyph in class 0.
                const auto classes = [&](size_t at, std::vector<uint16_t>& into) {
                    if (at == 0) {
                        into.assign(_glyph_count, 0);
                        return true;
                    }
                    return read_classdef(sub.from(at), _glyph_count, into);
                };
                if (!classes(sub.u16(8), out.class1) || !classes(sub.u16(10), out.class2) ||
                    sub_bad) {
                    warn("a GPOS pair subtable is malformed and was ignored");
                    continue;
                }
                // Without an XAdvance every cell is zero and none is stored;
                // the subtable still answers, with zero, for what it covers.
                if (has_advance) {
                    if (sub.n < 16 || cells * record > uint64_t(sub.n - 16)) {
                        warn("a GPOS pair subtable is malformed and was ignored");
                        continue;
                    }
                    if (cells > units_left) {
                        exhausted = true;
                        break;
                    }
                    units_left -= static_cast<size_t>(cells);
                    const size_t stride = static_cast<size_t>(record);
                    out.matrix.resize(static_cast<size_t>(cells));
                    for (size_t c = 0; c < out.matrix.size(); ++c)
                        out.matrix[c] = sub.s16(16 + c * stride + advance_at);
                }
                class_bytes_left -= class_bytes;
                out.covered.assign(_glyph_count, 0);
                for (const uint16_t glyph : coverage)
                    if (glyph < _glyph_count)
                        out.covered[glyph] = 1;
            } else {
                continue;
            }
            _kern.push_back(std::move(out));
        }
        if (bad)
            warn("a GPOS lookup is malformed and was ignored");
    }
    if (exhausted)
        warn("the GPOS pair subtables describe more than the table could hold, or more than "
             "is read; kerning from the rest was ignored");
}

size_t OpenTypeFont::kern_storage_bytes() const
{
    size_t bytes = 0;
    for (const KernSubtable& sub : _kern)
        bytes += sub.pair_keys.size() * 4 + sub.pair_values.size() * 2 + sub.covered.size() +
                 (sub.class1.size() + sub.class2.size() + sub.matrix.size()) * 2;
    return bytes;
}

int OpenTypeFont::kern_subtable_format(size_t index) const
{
    return index < _kern.size() ? _kern[index].format : 0;
}

namespace
{
// The key of a pair in a sorted pair list.
uint32_t pair_key(uint32_t left, uint32_t right)
{
    return (left << 16) | right;
}
} // namespace

// What one pair subtable says of a pair, if it says anything.
bool OpenTypeFont::subtable_kern(const KernSubtable& sub, uint32_t left, uint32_t right,
                                 int* value) const
{
    if (sub.format == 1) {
        if (left > 0xFFFFu || right > 0xFFFFu)
            return false;
        const uint32_t key = pair_key(left, right);
        const auto it = std::lower_bound(sub.pair_keys.begin(), sub.pair_keys.end(), key);
        if (it == sub.pair_keys.end() || *it != key)
            return false;
        *value = sub.pair_values[static_cast<size_t>(it - sub.pair_keys.begin())];
        return true;
    }
    if (left >= sub.covered.size() || !sub.covered[left])
        return false;
    const uint32_t c1 = sub.class1[left];
    const uint32_t c2 = right < sub.class2.size() ? sub.class2[right] : 0;
    *value = c1 >= sub.class1_count || c2 >= sub.class2_count || sub.matrix.empty()
                 ? 0
                 : sub.matrix[size_t(c1) * sub.class2_count + c2];
    return true;
}

int OpenTypeFont::legacy_kern(uint32_t left, uint32_t right) const
{
    if (left > 0xFFFFu || right > 0xFFFFu)
        return 0;
    const uint32_t key = pair_key(left, right);
    const auto it = std::lower_bound(_legacy_keys.begin(), _legacy_keys.end(), key);
    if (it == _legacy_keys.end() || *it != key)
        return 0;
    return _legacy_values[static_cast<size_t>(it - _legacy_keys.begin())];
}

int OpenTypeFont::pair_kern(uint32_t left, uint32_t right) const
{
    if (_kern.empty())
        return legacy_kern(left, right);
    int value = 0;
    for (const KernSubtable& sub : _kern)
        if (subtable_kern(sub, left, right, &value))
            return value;
    return 0;
}

int OpenTypeFont::pair_kern_accumulated(uint32_t left, uint32_t right) const
{
    if (_kern.empty())
        return legacy_kern(left, right);
    // Subtables are held in lookup order. A subtable two lookups share is held
    // once, under the first, so a pair it kerns counts once here where a
    // shaper would apply it under both.
    long long total = 0;
    for (size_t i = 0; i < _kern.size();) {
        const uint16_t lookup = _kern[i].lookup;
        bool applied = false;
        for (; i < _kern.size() && _kern[i].lookup == lookup; ++i) {
            int value = 0;
            if (!applied && subtable_kern(_kern[i], left, right, &value)) {
                total += value;
                applied = true;
            }
        }
    }
    return static_cast<int>(
        std::max<long long>(-0x7FFFFFFF, std::min<long long>(total, 0x7FFFFFFF)));
}

// --- The 'kern' table ----------------------------------------------------------------

void OpenTypeFont::read_legacy_kern()
{
    const Table kern = table("kern");
    if (!kern.present())
        return;
    bool bad = false;
    const Reader r{kern.data, kern.size, &bad};
    const unsigned version = r.u16(0);
    const size_t tables = r.u16(2);
    if (bad)
        return;
    if (version != 0) {
        warn("the kern table is not version 0 (Apple's form of it is not read) and was ignored");
        return;
    }
    // Every pair of every subtable that is read, with the subtable it came
    // from, so that one sort and one pass can combine them: sorting each
    // subtable into the total as it is met would cost the total again for
    // every subtable, and a table can hold thousands of one-pair subtables.
    struct Pair {
        uint32_t key;
        int16_t value;
        uint16_t subtable;
        bool replaces;
    };
    std::vector<Pair> pairs;
    size_t at = 4;
    bool cut = false;
    for (size_t t = 0; t < tables; ++t) {
        // version, length, format, coverage; then for format 0 the pair
        // count, three search fields, and six bytes a pair.
        if (!r.has(at, 6)) {
            cut = true;
            break;
        }
        const size_t length = r.u16(at + 2);
        const unsigned format = r.u8(at + 4), coverage = r.u8(at + 5);
        size_t extent = std::max<size_t>(length, 6);
        if (format == 0) {
            if (!r.has(at, 14)) {
                cut = true;
                break;
            }
            // The pair count says how long the subtable is. Its length field
            // cannot: it is 16 bits, and 10,920 pairs fill it.
            const size_t count = r.u16(at + 6);
            const size_t needed = 14 + count * 6;
            if (!r.has(at, needed)) {
                cut = true;
                break;
            }
            extent = std::max(extent, needed);
            // Coverage bit 0 is horizontal, bit 1 minimum values, bit 2
            // cross-stream, bit 3 override. Kerning along the line is bit 0
            // alone of the first three.
            if ((coverage & 0x07u) == 0x01u) {
                const bool replaces = (coverage & 0x08u) != 0;
                // No reserve() to the exact size here: that reallocates at
                // every subtable, which is the whole vector moved once each.
                for (size_t k = 0; k < count; ++k) {
                    const size_t record = at + 14 + k * 6;
                    pairs.push_back({pair_key(r.u16(record), r.u16(record + 2)), r.s16(record + 4),
                                     static_cast<uint16_t>(t), replaces});
                }
            }
        }
        at += extent;
    }
    if (cut)
        warn("the kern table is truncated; the subtables that are whole were read");
    // Stable, so a pair's values stay in subtable order and, within a
    // subtable, in file order.
    std::stable_sort(pairs.begin(), pairs.end(),
                     [](const Pair& a, const Pair& b) { return a.key < b.key; });
    for (size_t i = 0; i < pairs.size();) {
        const uint32_t key = pairs[i].key;
        // `before` is the total when the current subtable began: a pair a
        // subtable lists twice keeps the last of its values, not their sum.
        int32_t total = 0, before = 0;
        uint32_t subtable = 0x10000u;
        for (; i < pairs.size() && pairs[i].key == key; ++i) {
            if (pairs[i].subtable != subtable) {
                subtable = pairs[i].subtable;
                before = total;
            }
            total = pairs[i].replaces ? pairs[i].value : before + pairs[i].value;
        }
        _legacy_keys.push_back(key);
        _legacy_values.push_back(total);
    }
}

// --- MATH ------------------------------------------------------------------------------

void OpenTypeFont::read_math()
{
    const Table math = table("MATH");
    if (!math.present())
        return;
    bool bad = false;
    const Reader r{math.data, math.size, &bad};
    const size_t constants_at = r.u16(4), info_at = r.u16(6), variants_at = r.u16(8);
    if (bad || constants_at == 0) {
        warn("the MATH table has no constants and was ignored");
        return;
    }

    // Two int16 percentages, two uint16 heights, 51 MathValueRecords (a value
    // and a device table offset), and one last int16 percentage.
    const Reader c = r.from(constants_at);
    _math_constants[0] = c.s16(0);
    _math_constants[1] = c.s16(2);
    _math_constants[2] = c.u16(4);
    _math_constants[3] = c.u16(6);
    for (int i = 0; i < 51; ++i)
        _math_constants[4 + i] = c.s16(8 + size_t(i) * 4);
    _math_constants[55] = c.s16(8 + 51 * 4);
    if (bad) {
        std::fill(std::begin(_math_constants), std::end(_math_constants), 0);
        warn("the MATH constants are truncated; the table was ignored");
        return;
    }
    _has_math = true;

    // Per-glyph values: a coverage table and one MathValueRecord per covered
    // glyph. A glyph covered twice keeps its last value.
    const auto read_values = [&](size_t at, std::vector<GlyphValue>& out) {
        bool local_bad = false;
        Reader t = r.from(at);
        t.bad = &local_bad;
        std::vector<uint16_t> coverage;
        const size_t coverage_at = t.u16(0);
        if (coverage_at == 0 || !read_coverage(t.from(coverage_at), coverage) || local_bad)
            return false;
        const size_t count = std::min<size_t>(t.u16(2), coverage.size());
        if (!t.need(4, count * 4))
            return false;
        out.reserve(count);
        for (size_t i = 0; i < count; ++i)
            out.push_back({coverage[i], t.s16(4 + i * 4)});
        std::stable_sort(out.begin(), out.end(), [](const GlyphValue& a, const GlyphValue& b) {
            return a.glyph < b.glyph;
        });
        size_t kept = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (i + 1 == out.size() || out[i + 1].glyph != out[i].glyph)
                out[kept++] = out[i];
        out.resize(kept);
        return true;
    };
    if (info_at != 0) {
        bool info_bad = false;
        Reader info = r.from(info_at);
        info.bad = &info_bad;
        const size_t italics_at = info.u16(0), accents_at = info.u16(2);
        if (!info_bad && italics_at != 0 && !read_values(info_at + italics_at, _italics)) {
            _italics.clear();
            warn("the MATH italic corrections are malformed and were ignored");
        }
        if (!info_bad && accents_at != 0 && !read_values(info_at + accents_at, _top_accents)) {
            _top_accents.clear();
            warn("the MATH top accent attachments are malformed and were ignored");
        }
    }
    if (variants_at != 0) {
        read_math_variants(math, variants_at, true);
        read_math_variants(math, variants_at, false);
    }
}

void OpenTypeFont::read_math_variants(const Table& math, size_t offset, bool vertical)
{
    bool bad = false;
    const Reader r{math.data, math.size, &bad};
    const Reader v = r.from(offset);
    if (vertical)
        _min_connector_overlap = v.u16(0);
    const size_t coverage_at = v.u16(vertical ? 2 : 4);
    const size_t vert_count = v.u16(6), horiz_count = v.u16(8);
    Variants& out = vertical ? _vert : _horiz;
    if (bad || coverage_at == 0)
        return;
    std::vector<uint16_t> coverage;
    if (!read_coverage(v.from(coverage_at), coverage) || bad) {
        warn("a MATH variants coverage table is malformed and was ignored");
        return;
    }
    const size_t count = std::min(vertical ? vert_count : horiz_count, coverage.size());
    const size_t offsets_at = 10 + (vertical ? 0 : vert_count * 2);

    // (base glyph, position of its first record), sorted by glyph afterwards.
    struct Entry {
        uint16_t base;
        uint32_t first, count;
    };
    std::vector<Entry> entries;
    std::vector<MathVariantRecord> records;
    for (size_t i = 0; i < count && !bad; ++i) {
        const size_t construction_at = v.u16(offsets_at + i * 2);
        if (construction_at == 0)
            continue;
        const Reader construction = v.from(construction_at);
        const size_t n = construction.u16(2);
        if (n == 0 || !construction.need(4, n * 4))
            continue;
        // Constructions that do not overlap cannot hold more records than the
        // table has bytes.
        if (records.size() + n > math.size) {
            bad = true;
            break;
        }
        entries.push_back(
            {coverage[i], static_cast<uint32_t>(records.size()), static_cast<uint32_t>(n)});
        for (size_t k = 0; k < n; ++k)
            records.push_back({construction.u16(4 + k * 4), construction.u16(6 + k * 4)});
    }
    if (bad) {
        warn("a MATH glyph construction is malformed; the variants were ignored");
        return;
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& a, const Entry& b) { return a.base < b.base; });
    for (size_t i = 0; i < entries.size(); ++i) {
        // A glyph covered twice keeps its last construction.
        if (i + 1 < entries.size() && entries[i + 1].base == entries[i].base)
            continue;
        out.bases.push_back(entries[i].base);
        out.starts.push_back(static_cast<uint32_t>(out.records.size()));
        out.records.insert(out.records.end(), records.begin() + entries[i].first,
                           records.begin() + entries[i].first + entries[i].count);
    }
    out.starts.push_back(static_cast<uint32_t>(out.records.size()));
}

int OpenTypeFont::math_italic_correction(uint32_t glyph) const
{
    const auto it = std::lower_bound(_italics.begin(), _italics.end(), glyph,
                                     [](const GlyphValue& v, uint32_t g) { return v.glyph < g; });
    return it != _italics.end() && it->glyph == glyph ? it->value : 0;
}

bool OpenTypeFont::math_top_accent(uint32_t glyph, int* value) const
{
    const auto it = std::lower_bound(_top_accents.begin(), _top_accents.end(), glyph,
                                     [](const GlyphValue& v, uint32_t g) { return v.glyph < g; });
    if (it == _top_accents.end() || it->glyph != glyph)
        return false;
    if (value != nullptr)
        *value = it->value;
    return true;
}

MathVariantList OpenTypeFont::variants_of(const Variants& v, uint32_t glyph) const
{
    const auto it = std::lower_bound(v.bases.begin(), v.bases.end(), glyph);
    if (it == v.bases.end() || *it != glyph)
        return MathVariantList{};
    const size_t index = static_cast<size_t>(it - v.bases.begin());
    return MathVariantList{v.records.data() + v.starts[index],
                           v.starts[index + 1] - v.starts[index]};
}

MathVariantList OpenTypeFont::math_vertical_variants(uint32_t glyph) const
{
    return variants_of(_vert, glyph);
}

MathVariantList OpenTypeFont::math_horizontal_variants(uint32_t glyph) const
{
    return variants_of(_horiz, glyph);
}

} // namespace ImTypeset::detail
