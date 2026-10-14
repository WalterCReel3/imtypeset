// A reader for the part of an OpenType file that layout needs, and no more:
// what the reference's advtext/fontlib.py reads through fontTools.
//
//   - the table directory of a single font or of one face of a collection
//     ('ttcf', by face index); head, maxp, hhea, OS/2 (sxHeight), hmtx
//   - cmap: the subtable fontTools' getBestCmap() picks, in format 0, 4, 6,
//     10, 12 or 13; codepoints mapped to glyph 0 are not mappings
//   - glyph ink boxes: the exact extent of the outline, from glyf (simple and
//     composite) or from CFF, name-keyed or CID-keyed (a Type 2 charstring
//     interpreter)
//   - GPOS: the PairPos subtables (formats 1 and 2, through Extension lookups
//     too) of every feature tagged 'kern', horizontal advance of the first
//     glyph only
//   - the 'kern' table, version 0, format 0 subtables, as the kerning of a
//     font whose GPOS gives none (an option, on unless turned off)
//   - MATH: the constants, italic corrections, top accent attachments, and the
//     variant lists of the vertical and horizontal constructions
//   - glyph names, from the CFF charset or from post (formats 1 and 2), which
//     decide the order synthetic codepoints are minted in
//
// Everything is in font units and by glyph id. Codepoints, sizes and roles are
// src/fontlib's.
//
// NOT READ, and what happens instead:
//   - WOFF, WOFF2, Type 1, CFF2, fonts with only bitmaps: load() fails and the
//     reason names the format
//   - a cmap subtable in format 2, 8 or 14: passed over for the next subtable
//     in preference order
//   - variable-font axes (fvar, gvar, HVAR): the default instance is what is read
//   - GPOS lookups other than PairPos; a pair's second value record; device
//     tables; lookup flags; script and language selection (every 'kern'
//     feature record counts, whoever reaches it)
//   - 'kern' subtables that are not format 0, are vertical, cross-stream or
//     hold minimum values; the Apple form of the table (version 1); kerx
//   - glyph assemblies of the MATH table (the extensible constructions)
//   - glyf components placed by point matching: the component is skipped
//   - the Type 2 'random' operator and seac-style endchar: the glyph's
//     outline ends there
//
// WHY NOT imstb_truetype.h, which ImGui vendors and which reads some of this:
// its glyph box is the glyf header's four integers for TrueType and, for CFF,
// the integer extent of on-curve and control points alike. The reference's box
// is the exact extent of the curves in doubles, and layout rounds heights and
// depths from it; the two differ for hundreds of glyphs per bundled face
// (GLYPHSTAT in tests/golden/font). Its GPOS walk takes every PairPos lookup
// whatever feature names it and does not follow Extension lookups, and it
// reads no MATH table and no glyph names.
//
// THE BYTES ARE UNTRUSTED. Every offset and count read from the file is checked
// against the table it points into before it is used. A file that is truncated
// or malformed in a table every font must have fails load() with a reason. A
// defect confined to one glyph's outline or to an optional table (GPOS, MATH)
// does not fail the load: the glyph's box is empty, or the table is treated as
// absent, and `warnings()` says so. Nothing here asserts, and no input makes it
// read outside the buffer, recurse without bound or loop without bound.
//
// The font does not own its bytes: the buffer handed to load() must outlive it.
// Not thread-safe: ink boxes are computed on first request and remembered.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ImTypeset::detail
{

// The ink extent of a glyph in font units, y up, measured from its origin. All
// zero for a glyph with no outline.
struct GlyphBounds {
    double x_min = 0.0;
    double y_min = 0.0;
    double x_max = 0.0;
    double y_max = 0.0;

    friend bool operator==(const GlyphBounds& a, const GlyphBounds& b)
    {
        return a.x_min == b.x_min && a.y_min == b.y_min && a.x_max == b.x_max && a.y_max == b.y_max;
    }
};

// One entry of a MATH variant list: a glyph and its size along the axis the
// construction grows on (OpenType's AdvanceMeasurement).
struct MathVariantRecord {
    uint16_t glyph = 0;
    uint16_t advance = 0;
};

struct MathVariantList {
    const MathVariantRecord* data = nullptr;
    size_t size = 0;

    const MathVariantRecord* begin() const { return data; }
    const MathVariantRecord* end() const { return data + size; }
    bool empty() const { return size == 0; }
};

// The MathConstants table has 56 entries; this is their order in the file,
// which is the order the OpenType specification lists them in.
constexpr int MATH_CONSTANT_COUNT = 56;

enum MathConstant : int {
    ScriptPercentScaleDown = 0,
    ScriptScriptPercentScaleDown,
    DelimitedSubFormulaMinHeight,
    DisplayOperatorMinHeight,
    MathLeading,
    AxisHeight,
    AccentBaseHeight,
    FlattenedAccentBaseHeight,
    SubscriptShiftDown,
    SubscriptTopMax,
    SubscriptBaselineDropMin,
    SuperscriptShiftUp,
    SuperscriptShiftUpCramped,
    SuperscriptBottomMin,
    SuperscriptBaselineDropMax,
    SubSuperscriptGapMin,
    SuperscriptBottomMaxWithSubscript,
    SpaceAfterScript,
    UpperLimitGapMin,
    UpperLimitBaselineRiseMin,
    LowerLimitGapMin,
    LowerLimitBaselineDropMin,
    StackTopShiftUp,
    StackTopDisplayStyleShiftUp,
    StackBottomShiftDown,
    StackBottomDisplayStyleShiftDown,
    StackGapMin,
    StackDisplayStyleGapMin,
    StretchStackTopShiftUp,
    StretchStackBottomShiftDown,
    StretchStackGapAboveMin,
    StretchStackGapBelowMin,
    FractionNumeratorShiftUp,
    FractionNumeratorDisplayStyleShiftUp,
    FractionDenominatorShiftDown,
    FractionDenominatorDisplayStyleShiftDown,
    FractionNumeratorGapMin,
    FractionNumDisplayStyleGapMin,
    FractionRuleThickness,
    FractionDenominatorGapMin,
    FractionDenomDisplayStyleGapMin,
    SkewedFractionHorizontalGap,
    SkewedFractionVerticalGap,
    OverbarVerticalGap,
    OverbarRuleThickness,
    OverbarExtraAscender,
    UnderbarVerticalGap,
    UnderbarRuleThickness,
    UnderbarExtraDescender,
    RadicalVerticalGap,
    RadicalDisplayStyleVerticalGap,
    RadicalRuleThickness,
    RadicalExtraAscender,
    RadicalKernBeforeDegree,
    RadicalKernAfterDegree,
    RadicalDegreeBottomRaisePercent,
};

// The OpenType name of a MathConstants entry, or "" out of range.
const char* math_constant_name(int index);

// One cmap run: codepoints first..last map to consecutive glyph ids from
// `glyph`, or, when `one_glyph` is set, all of them to `glyph` (a group of a
// format 13 subtable, which may span the whole code space).
struct CmapRange {
    uint32_t first = 0;
    uint32_t last = 0;
    uint32_t glyph = 0;
    bool one_glyph = false;

    // The glyph of a codepoint in first..last.
    uint32_t glyph_of(uint32_t codepoint) const
    {
        return one_glyph ? glyph : glyph + (codepoint - first);
    }
    // The highest glyph id the run maps to. No run's ids pass 2^32 - 1.
    uint32_t last_glyph() const { return glyph_of(last); }
};

// A glyf glyph whose header box must be raised in the font a rasteriser is
// given (see OpenTypeFont::atlas_box_repairs).
struct GlyphBoxRepair {
    uint32_t glyph = 0;
    // Where in the glyf table the glyph's data starts; yMax is 8 bytes on.
    size_t glyf_offset = 0;
    int16_t y_max = 0;
};

// What load() is asked beyond the bytes.
struct FontLoadOptions {
    // Which face of a collection ('ttcf'). A file that is not a collection
    // has face 0 only.
    uint32_t face_index = 0;
    // Whether a font whose GPOS has no pair kerning takes its kerning from a
    // version 0 'kern' table. The reference never reads that table; this is
    // an extension, and turning it off gives the reference's answers.
    bool legacy_kern = true;
};

// One table of the face that was loaded: its tag and its bytes in the file.
struct TableRecord {
    uint32_t tag = 0;
    const uint8_t* data = nullptr;
    size_t size = 0;
};

class OpenTypeFont
{
public:
    // Parses `size` bytes at `data`. False, with `why` set (if not null), when
    // the file cannot be used; the object is then empty and every query
    // answers zero. The buffer must stay valid for the object's life.
    bool load(const uint8_t* data, size_t size, std::string* why = nullptr,
              const FontLoadOptions& options = FontLoadOptions());

    bool loaded() const { return _loaded; }

    // How many faces a file holds: the count a collection states, 1 for
    // anything else that is long enough to have a version tag, 0 otherwise.
    // Says nothing about whether any of them loads.
    static uint32_t faces_in(const uint8_t* data, size_t size);

    // The face that was loaded, and how many its file holds.
    uint32_t face_index() const { return _face_index; }
    uint32_t face_count() const { return _face_count; }
    // The sfnt version of the face: 0x00010000, 'true' or 'OTTO'.
    uint32_t sfnt_version() const { return _sfnt_version; }
    // The face's table directory as it was accepted: every table lies inside
    // the file and no tag appears twice. In directory order.
    size_t table_count() const { return _directory.size(); }
    TableRecord table_record(size_t index) const;

    // The strict pass: true if everything a rasteriser will follow in this
    // file is in bounds and bounded, so that the file can be handed to one
    // that checks nothing itself (stb_truetype). load() accepts more than
    // this does, on purpose: a font used only for metrics may have a broken
    // glyph. False, with `why` set, when the file must not go to an atlas.
    // The answer is computed once (it reads every glyph) and remembered.
    //
    // Beyond what load() requires: tables that do not add up to more than the
    // file; a version tag that matches the outlines, and one kind of outline;
    // head and hhea of full length, unitsPerEm in 16..16384, an ascender above
    // the descender, hmtx long enough for every glyph. For glyf: loca in order
    // and inside glyf; every glyph's contour ends, flags and coordinates
    // inside its own data; every composite's components inside its data, in
    // range, placed by offset, free of cycles, nested and expanding within
    // bounds; a loca table with its last entry, and at least one full hmtx
    // record. For CFF: nothing in the DICTs tolerated or ambiguous, a
    // charstring per glyph, no charstring that exhausts a work budget, and
    // in a CID-keyed font a Font DICT the font has for every glyph.
    // A charstring's arithmetic leaves no coordinate that is not a number.
    // For both: no outline reaching further than 16 times the font's
    // ascender-to-descender height from the origin.
    //
    // One thing it passes is safe only once repaired, and font_loader's
    // build_atlas_font, which writes the file a rasteriser is given, repairs
    // it: see atlas_box_repairs(). The file as it stands is not what goes to
    // a rasteriser.
    bool strict_check(std::string* why = nullptr) const;

    // The glyf glyphs whose header says their box ends at or below the
    // baseline and which have a point of an edge above it, each with the
    // yMax that holds its points (for a composite, an outer bound of them).
    // stb_truetype sizes a glyph's bitmap by the header's box and asserts in
    // its rasteriser on an edge wholly above a bitmap that starts at row 0,
    // which is the box these headers describe; installed fonts have such
    // glyphs (NanumBarunGothic.ttf has one). With yMax raised the bitmap
    // holds the outline. Runs the strict pass if it has not run; empty if
    // the font does not pass it, and for CFF, where the rasteriser computes
    // the box from the charstring.
    const std::vector<GlyphBoxRepair>& atlas_box_repairs() const;

    // What was tolerated while loading or while reading a glyph: one line each.
    const std::vector<std::string>& warnings() const { return _warnings; }

    // --- head, maxp, hhea, OS/2 ---------------------------------------------
    bool is_cff() const { return _is_cff; }
    int units_per_em() const { return _units_per_em; }
    uint32_t glyph_count() const { return _glyph_count; }
    int hhea_ascender() const { return _ascender; }
    int hhea_descender() const { return _descender; }
    int hhea_line_gap() const { return _line_gap; }
    uint32_t hmetric_count() const { return _hmetric_count; }
    // OS/2.sxHeight, or 0 when the table is absent or older than version 2.
    int os2_x_height() const { return _x_height; }

    // --- cmap -----------------------------------------------------------------
    // The subtable picked, as (platform, encoding, format); all zero if none.
    // The first, in fontTools' getBestCmap() order — (3,10), (0,6), (0,4),
    // (3,1), (0,3), (0,2), (0,1), (0,0) — that is in a format this reader
    // reads. A font with none of those is read through its Windows Symbol
    // subtable (3,0), codes as they stand, or failing that its Macintosh
    // Roman one (1,0), codes taken to Unicode; the reference refuses both.
    int cmap_platform() const { return _cmap_platform; }
    int cmap_encoding() const { return _cmap_encoding; }
    int cmap_format() const { return _cmap_format; }
    // Sorted by `first`, not overlapping. No range maps a codepoint to glyph
    // 0: a codepoint the file maps there is one the font does not have, which
    // is how fontTools and so the reference read it.
    const std::vector<CmapRange>& cmap() const { return _cmap; }
    // True, with `*glyph` set, if the font maps the codepoint. The glyph may
    // be past the last glyph, if that is what the file says; it is never 0.
    // A format 12 group whose glyph ids would run past 2^32 - 1 maps only the
    // codepoints whose ids do not.
    bool lookup(uint32_t codepoint, uint32_t* glyph) const;

    // --- hmtx -----------------------------------------------------------------
    // Zero for a glyph id past the last glyph.
    int advance(uint32_t glyph) const { return glyph < _advances.size() ? _advances[glyph] : 0; }
    int left_side_bearing(uint32_t glyph) const { return glyph < _lsbs.size() ? _lsbs[glyph] : 0; }

    // --- outlines ---------------------------------------------------------------
    // The exact extent of the glyph's curves, computed as fontTools' BoundsPen
    // computes it (tools/oracle/FORMAT-font.md, "How the ink box is computed"),
    // so the doubles are the reference's to the last bit. Empty for a glyph id
    // out of range, a glyph with no outline, and a glyph whose data is broken
    // or, in CFF, computes a coordinate that is not finite. Never infinite or
    // NaN.
    GlyphBounds bounds(uint32_t glyph) const;
    // How many outlines bounds() has run since the font was loaded: once a
    // glyph, since a box is remembered. For tests, which hold a hostile glyph
    // to being run once rather than to a time.
    size_t outlines_run() const { return _outlines_run; }

    // --- Pair kerning -------------------------------------------------------------
    // The 'kern' feature's adjustment to the first glyph's advance for an
    // adjacent pair, in font units; zero when no subtable has a value. The
    // first GPOS subtable with a value answers: a format 1 subtable has one
    // only for a pair it lists, a format 2 subtable for every pair whose
    // first glyph it covers. A font with no GPOS pair subtable at all answers
    // from its 'kern' table instead, when that was read (legacy_kern_used()).
    int pair_kern(uint32_t left, uint32_t right) const;
    size_t kern_subtable_count() const { return _kern.size(); }
    // 1 or 2; 0 out of range.
    int kern_subtable_format(size_t index) const;
    // The bytes the kerning read from GPOS is held in.
    size_t kern_storage_bytes() const;
    // What a shaper would answer from the same subtables and pair_kern() does
    // not: within one lookup the first subtable that applies, and the lookups'
    // values added together. Lookup flags, script and language are ignored
    // here as they are in pair_kern(). For measuring how far the two differ.
    int pair_kern_accumulated(uint32_t left, uint32_t right) const;

    // The 'kern' table, version 0: every pair of its horizontal format 0
    // subtables that hold kerning values (not minimums, not cross-stream), as
    // (left << 16 | right) ascending with the value beside it. Subtables add
    // up, except that one with the override bit replaces what came before it;
    // a pair listed twice in one subtable keeps its last value. Read whenever
    // the option asks for it and the table is there.
    const std::vector<uint32_t>& legacy_kern_keys() const { return _legacy_keys; }
    const std::vector<int32_t>& legacy_kern_values() const { return _legacy_values; }
    // True when pair_kern() answers from those pairs: the option was on, the
    // table gave at least one pair, and GPOS gave no pair subtable.
    bool legacy_kern_used() const { return _kern.empty() && !_legacy_keys.empty(); }

    // --- MATH ---------------------------------------------------------------------
    bool has_math() const { return _has_math; }
    // Zero without a MATH table or out of range. A MathValueRecord's device
    // table is ignored.
    int math_constant(int index) const
    {
        return _has_math && index >= 0 && index < MATH_CONSTANT_COUNT ? _math_constants[index] : 0;
    }
    int math_italic_correction(uint32_t glyph) const;
    // False if the glyph has no top accent attachment.
    bool math_top_accent(uint32_t glyph, int* value) const;
    // The variant list of a glyph's vertical or horizontal construction, in
    // file order; empty if it has none.
    MathVariantList math_vertical_variants(uint32_t glyph) const;
    MathVariantList math_horizontal_variants(uint32_t glyph) const;
    // Every glyph with a non-empty vertical variant list, ascending.
    const std::vector<uint16_t>& math_vertical_bases() const { return _vert.bases; }
    const std::vector<uint16_t>& math_horizontal_bases() const { return _horiz.bases; }
    int math_min_connector_overlap() const { return _min_connector_overlap; }

    // --- names --------------------------------------------------------------------
    // The CFF charset's name for a glyph, as bytes. Empty for a TrueType font,
    // for a CID-keyed font (whose glyphs have numbers, not names) and for a
    // glyph id out of range. Views into the font's buffer or into a static
    // table.
    std::string_view glyph_name(uint32_t glyph) const;
    // True for a CFF font whose charset holds CIDs.
    bool is_cid_keyed() const { return _is_cid; }
    // A name for every glyph, as fontTools' glyph order has them, which is
    // what the reference sorts by: the CFF charset's names, "cidNNNNN" for a
    // CID-keyed font; for TrueType the names of a format 1 or 2 post table,
    // an unnamed glyph being "glyphNNNNN" and a name met again getting a
    // numbered suffix. False, leaving `names` empty, when the font has no
    // such names (post format 3, or none); fontTools then makes names up from
    // the cmap, and a glyph the cmap does not reach is "glyphNNNNN".
    bool glyph_order_names(std::vector<std::string>& names) const;

private:
    struct Table {
        const uint8_t* data = nullptr;
        size_t size = 0;
        bool present() const { return data != nullptr; }
    };

    struct KernSubtable {
        int format = 0;
        // The lookup it came from, for pair_kern_accumulated.
        uint16_t lookup = 0;
        // Format 1: (left << 16 | right) ascending, with the value beside it.
        std::vector<uint32_t> pair_keys;
        std::vector<int16_t> pair_values;
        // Format 2: per glyph id, whether covered and its two classes; then
        // the class1 x class2 matrix, row-major.
        std::vector<uint8_t> covered;
        std::vector<uint16_t> class1;
        std::vector<uint16_t> class2;
        uint32_t class1_count = 0;
        uint32_t class2_count = 0;
        std::vector<int16_t> matrix;
    };

    struct GlyphValue {
        uint16_t glyph = 0;
        int16_t value = 0;
    };

    struct Variants {
        std::vector<uint16_t> bases;  // ascending
        std::vector<uint32_t> starts; // bases.size() + 1 offsets into records
        std::vector<MathVariantRecord> records;
    };

    struct BoundsPen;
    struct CffState;
    struct StrictGlyph;

    const char* strict_reason() const;
    const char* strict_glyf(double reach) const;
    const char* strict_glyf_glyph(uint32_t glyph, std::vector<StrictGlyph>& facts, int depth,
                                  double reach) const;
    const char* strict_cff(double reach) const;

    void reset();
    bool fail(std::string* why, const std::string& reason);
    void warn(const char* what) const;
    Table table(const char* tag) const;

    bool read_directory(std::string* why, uint32_t face_index);
    bool read_head_maxp_hhea(std::string* why);
    void read_os2();
    bool read_hmtx(std::string* why);
    bool read_cmap(std::string* why);
    // 1 read, 0 not in a format that is read, -1 malformed (`why` is set).
    int read_cmap_subtable(const Table& cmap, size_t offset, int mapping, std::string* why);
    bool read_outline_tables(std::string* why);
    bool read_cff(std::string* why);
    void read_gpos();
    void read_legacy_kern();
    bool subtable_kern(const KernSubtable& sub, uint32_t left, uint32_t right, int* value) const;
    int legacy_kern(uint32_t left, uint32_t right) const;
    const char* read_cff_private(double size_value, double offset_value, size_t* subrs_at);
    void read_math();
    void read_math_variants(const Table& math, size_t offset, bool vertical);

    bool draw_glyf(uint32_t glyph, BoundsPen& pen, const double* transforms, int depth,
                   bool top_level, int& budget) const;
    bool draw_cff(uint32_t glyph, BoundsPen& pen) const;
    bool run_charstring(const uint8_t* code, size_t size, CffState& state, int depth) const;
    const std::vector<uint32_t>* local_subrs_of(uint32_t glyph) const;
    MathVariantList variants_of(const Variants& v, uint32_t glyph) const;

    const uint8_t* _data = nullptr;
    size_t _size = 0;
    bool _loaded = false;
    bool _is_cff = false;
    mutable std::vector<std::string> _warnings;

    struct DirectoryEntry {
        uint32_t tag = 0;
        uint32_t offset = 0;
        uint32_t length = 0;
    };
    std::vector<DirectoryEntry> _directory;
    uint32_t _sfnt_version = 0;
    uint32_t _face_index = 0;
    uint32_t _face_count = 0;
    uint64_t _table_bytes = 0; // of every table in _directory, summed

    // 0 not asked yet, 1 passed, -1 refused for _strict_why.
    mutable int _strict_state = 0;
    mutable std::string _strict_why;
    mutable std::vector<GlyphBoxRepair> _box_repairs;
    // What read_cff put up with that a rasteriser would follow; null if nothing.
    const char* _cff_tolerated = nullptr;

    int _units_per_em = 0;
    int _index_to_loc_format = 0;
    uint32_t _glyph_count = 0;
    int _ascender = 0;
    int _descender = 0;
    int _line_gap = 0;
    uint32_t _hmetric_count = 0;
    int _x_height = 0;

    int _cmap_platform = 0;
    int _cmap_encoding = 0;
    int _cmap_format = 0;
    std::vector<CmapRange> _cmap;

    std::vector<uint16_t> _advances;
    std::vector<int16_t> _lsbs;

    // glyf
    Table _glyf;
    std::vector<uint32_t> _loca;     // glyph_count + 1 offsets into glyf
    bool _loca_unterminated = false; // the table lacks its last entry
    // CFF
    Table _cff;
    std::vector<uint32_t> _charstring_offsets; // glyph_count + 1, into _cff
    std::vector<uint32_t> _global_subr_offsets;
    // Each distinct local subroutine INDEX, as its count + 1 offsets; the
    // first is empty and stands for "none". `_fd_subrs` says which of them a
    // Font DICT's Private DICT names: one entry for a name-keyed font, one per
    // Font DICT for a CID-keyed font.
    std::vector<std::vector<uint32_t>> _local_subr_offsets;
    std::vector<uint16_t> _fd_subrs;
    std::vector<uint16_t> _fd_select;      // CID-keyed: the Font DICT of each glyph
    std::vector<uint32_t> _string_offsets; // String INDEX, count + 1
    std::vector<uint16_t> _charset;        // SID, or CID, per glyph
    bool _is_cid = false;
    bool _charset_unread = false;

    mutable std::vector<GlyphBounds> _bounds;
    mutable std::vector<uint8_t> _bounds_known;
    mutable size_t _outlines_run = 0;

    std::vector<KernSubtable> _kern;
    std::vector<uint32_t> _legacy_keys;
    std::vector<int32_t> _legacy_values;

    bool _has_math = false;
    int _math_constants[MATH_CONSTANT_COUNT] = {};
    std::vector<GlyphValue> _italics;     // ascending glyph
    std::vector<GlyphValue> _top_accents; // ascending glyph
    Variants _vert;
    Variants _horiz;
    int _min_connector_overlap = 0;
};

} // namespace ImTypeset::detail
