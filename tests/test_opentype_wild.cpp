// src/opentype and src/fontlib against the real-font corpus digests in
// tests/golden/font-wild (tools/oracle/FORMAT-font-wild.md): every font that
// was installed on the machine the goldens were dumped on, as fontTools and the
// reference answered it.
//
// The fonts are not in the repository. A face is loaded from the absolute path
// its digest records, and only if the file there has the recorded length and
// SHA-256; otherwise it is SKIPPED and counted. With no font present every
// face is skipped and the test passes having compared nothing, and says so.
//
// Each face that is present ends in exactly one of:
//   refused     the reader would not load it, with a reason (allowed, counted)
//   read        loaded, and every compared value equals the reference's
//   mismatched  loaded, and at least one value differs: the test FAILS
// A crash or a sanitiser report fails by itself.
//
// By default every WILD_STRIDE-th face is visited, plus every face with a MATH
// table, so the ctest run stays short. IMTYPESET_WILD_ALL=1 visits all of them;
// IMTYPESET_WILD_STRIDE=n picks another stride; IMTYPESET_WILD_TRACE=1 prints
// each face before it is loaded, which names the font if the reader crashes.
//
// What is compared for a loaded face, all in font units and by glyph id:
// flavour, unitsPerEm, glyph count, hhea metrics, the picked x-height, the
// picked cmap subtable, the whole cmap (count and hash) and its samples, every
// advance (hash) and the samples, every ink box (exact hash, rounded hash, 16
// chunk hashes) and the samples bit for bit, the kern subtable count and
// formats, the sampled kern pairs, and for a MATH font the constants, sampled
// italic corrections, every vertical variant list, the minted codepoints, and
// MathParams at one size.
//
// Not compared, because the reader does not expose it: the contents of each
// kern subtable (KSUB's hash), the MATH italic-correction table as a whole
// (MATHITALIC's hash), and the BYTES-only records (GLYF, KSHAPE, CFF, ...),
// which are used here to say which features a face that read correctly has.

#include "golden.h"
#include "wild_corpus.h"

#include "fontlib.h"
#include "fontmetrics.h"
#include "opentype.h"
#include "units.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

constexpr int WILD_STRIDE = 8;

// --- The digest's hash: FNV-1a folding a 64-bit word per step -------------------

struct H64 {
    uint64_t h = 0xcbf29ce484222325ull;
    void word(uint64_t w) { h = (h ^ w) * 0x100000001b3ull; }
    void integer(int64_t v) { word(static_cast<uint64_t>(v)); }
    // The double's bits, with -0.0 folded onto +0.0 as the oracle folds it.
    void real(double v)
    {
        const double folded = v + 0.0;
        uint64_t bits = 0;
        std::memcpy(&bits, &folded, sizeof bits);
        word(bits);
    }
    std::string hex() const
    {
        char out[17];
        std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
        return std::string(out, 16);
    }
};

// --- The tally ------------------------------------------------------------------

struct Count {
    long faces = 0;
    std::string example;
};

struct Tally {
    long total = 0;       // FACE records in the goldens
    long not_sampled = 0; // left out by the stride
    long visited = 0;
    long skipped_absent = 0;
    long skipped_changed = 0;
    long refused = 0;
    long read = 0;
    long mismatched = 0;
    long glyphs_boxed = 0;
    long kern_pairs = 0;
    // Sampled pairs for which adding the kern lookups up, as OpenType says a
    // shaper does, gives another value than the first subtable with one.
    long accumulate_faces = 0;   // faces with more than one kern lookup
    long accumulate_pairs = 0;   // sampled pairs of those faces
    long accumulate_nonzero = 0; // of them, pairs either rule kerns
    long accumulate_differ = 0;  // of them, pairs where the rules differ
    long accumulate_differ_faces = 0;
    int accumulate_worst = 0; // largest difference, in font units
    std::string accumulate_example;
    std::map<std::string, Count> refusals;         // reader's reason, generalised
    std::map<std::string, Count> mismatch_classes; // faces showing the class
    std::map<std::string, Count> reference_raised; // what we did where it raised
    std::map<std::string, Count> verified;         // features of faces that read correctly
    std::map<std::string, Count> warnings;         // reader warnings on loaded faces

    static void add(std::map<std::string, Count>& into, const std::string& key,
                    const std::string& example)
    {
        Count& c = into[key];
        if (c.faces++ == 0)
            c.example = example;
    }
};

// A reason with its particulars removed, so reasons that differ only in a
// number or a tag fall in one bucket.
std::string generalise(std::string_view text)
{
    std::string out;
    bool in_number = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            if (!in_number)
                out += 'N';
            in_number = true;
        } else {
            in_number = false;
            out += c;
        }
    }
    return out;
}

struct FaceDigest {
    std::string path;
    int index = 0;
    int face_count = 0;
    int64_t byte_length = 0;
    std::string sha256;
    std::vector<const golden::Record*> records;

    const golden::Record* first(std::string_view tag) const
    {
        for (const golden::Record* r : records)
            if (r->is(tag))
                return r;
        return nullptr;
    }
    std::string label() const { return path + "#" + std::to_string(index); }
};

// One face's comparison: the classes of difference found, each with the first
// instance as its detail.
struct Findings {
    std::vector<std::pair<std::string, std::string>> classes;

    void add(const std::string& cls, const std::string& detail)
    {
        for (const auto& c : classes)
            if (c.first == cls)
                return;
        classes.emplace_back(cls, detail);
    }
    bool has(std::string_view cls) const
    {
        for (const auto& c : classes)
            if (c.first == cls)
                return true;
        return false;
    }
};

bool same_bits(double a, double b)
{
    return std::memcmp(&a, &b, sizeof a) == 0 || (a == 0.0 && b == 0.0);
}

const double* math_param(const MathParams& p, std::string_view name)
{
    struct Field {
        const char* name;
        double MathParams::*member;
    };
    static const Field fields[] = {
        {"math_x_height", &MathParams::math_x_height},
        {"math_quad", &MathParams::math_quad},
        {"num1", &MathParams::num1},
        {"num2", &MathParams::num2},
        {"num3", &MathParams::num3},
        {"denom1", &MathParams::denom1},
        {"denom2", &MathParams::denom2},
        {"sup1", &MathParams::sup1},
        {"sup2", &MathParams::sup2},
        {"sup3", &MathParams::sup3},
        {"sub1", &MathParams::sub1},
        {"sub2", &MathParams::sub2},
        {"sup_drop", &MathParams::sup_drop},
        {"sub_drop", &MathParams::sub_drop},
        {"delim1", &MathParams::delim1},
        {"delim2", &MathParams::delim2},
        {"axis_height", &MathParams::axis_height},
        {"default_rule_thickness", &MathParams::default_rule_thickness},
        {"big_op_spacing1", &MathParams::big_op_spacing1},
        {"big_op_spacing2", &MathParams::big_op_spacing2},
        {"big_op_spacing3", &MathParams::big_op_spacing3},
        {"big_op_spacing4", &MathParams::big_op_spacing4},
        {"big_op_spacing5", &MathParams::big_op_spacing5},
        {"radical_rule_thickness", &MathParams::radical_rule_thickness},
        {"radical_vertical_gap", &MathParams::radical_vertical_gap},
        {"radical_display_style_vertical_gap", &MathParams::radical_display_style_vertical_gap},
        {"script_scale", &MathParams::script_scale},
        {"script_script_scale", &MathParams::script_script_scale},
        {"display_operator_min_height", &MathParams::display_operator_min_height},
    };
    for (const Field& f : fields)
        if (name == f.name)
            return &(p.*(f.member));
    return nullptr;
}

void compare_tables(const FaceDigest& face, const OpenTypeFont& font, const FaceData& data,
                    Findings& out)
{
    if (const golden::Record* r = face.first("SFNT")) {
        const std::string flavour = r->str(1);
        const bool want_cff = flavour == "CFF" || flavour == "CFF2";
        if (font.is_cff() != want_cff)
            out.add("flavour: is_cff() disagrees with the outline tables present", flavour);
    }
    if (const golden::Record* r = face.first("PICK")) {
        if (font.units_per_em() != r->i32(0))
            out.add("head: unitsPerEm",
                    check::format("%d, reference %d", font.units_per_em(), r->i32(0)));
        if (data.ascent() != r->i32(1) || data.descent() != r->i32(2))
            out.add("hhea: ascent or descent",
                    check::format("%d/%d, reference %d/%d", data.ascent(), data.descent(),
                                  r->i32(1), r->i32(2)));
        if (data.x_height() != r->i32(3))
            out.add(r->str(4) == "fallback" ? "x-height: the 0.45 em fallback"
                                            : "x-height: OS/2.sxHeight",
                    check::format("%d, reference %d", data.x_height(), r->i32(3)));
    }
    if (const golden::Record* r = face.first("GLYPHORDER")) {
        if (static_cast<int64_t>(font.glyph_count()) != r->i64(0))
            out.add("maxp: glyph count", check::format("%u, reference %lld", font.glyph_count(),
                                                       static_cast<long long>(r->i64(0))));
    }
    if (const golden::Record* r = face.first("HHEA")) {
        if (font.hhea_line_gap() != r->i32(3) ||
            static_cast<int64_t>(font.hmetric_count()) != r->i64(4))
            out.add("hhea: lineGap or numberOfHMetrics", "");
    }
}

void compare_cmap(const FaceDigest& face, const OpenTypeFont& font, Findings& out)
{
    for (const golden::Record* r : face.records) {
        if (!r->is("CMAPSUB") || !r->boolean(5))
            continue;
        if (font.cmap_platform() != r->i32(0) || font.cmap_encoding() != r->i32(1) ||
            font.cmap_format() != r->i32(2))
            out.add("cmap: a different subtable was picked",
                    check::format("(%d,%d) format %d, reference (%d,%d) format %d",
                                  font.cmap_platform(), font.cmap_encoding(), font.cmap_format(),
                                  r->i32(0), r->i32(1), r->i32(2)));
    }
    const golden::Record* r = face.first("CMAP");
    if (r == nullptr)
        return;
    H64 all, without_zero;
    int64_t count_all = 0, count_without_zero = 0;
    for (const CmapRange& range : font.cmap()) {
        for (uint64_t cp = range.first; cp <= range.last; ++cp) {
            const uint64_t glyph = range.glyph_of(static_cast<uint32_t>(cp));
            all.word(cp);
            all.word(glyph);
            ++count_all;
            if (glyph != 0) {
                without_zero.word(cp);
                without_zero.word(glyph);
                ++count_without_zero;
            }
        }
    }
    const int64_t want_count = r->i64(0);
    const std::string want_hash = r->str(1);
    if (count_all != want_count || all.hex() != want_hash) {
        if (count_without_zero == want_count && without_zero.hex() == want_hash)
            out.add("cmap: keeps codepoints the file maps to glyph 0, which the reference drops",
                    check::format("%lld entries, reference %lld", static_cast<long long>(count_all),
                                  static_cast<long long>(want_count)));
        else
            out.add("cmap: the mapping differs",
                    check::format("%lld entries (%lld not to glyph 0), reference %lld",
                                  static_cast<long long>(count_all),
                                  static_cast<long long>(count_without_zero),
                                  static_cast<long long>(want_count)));
    }
    if (const golden::Record* s = face.first("CMAPS")) {
        golden::Cursor c(*s);
        const int64_t n = c.i64();
        for (int64_t i = 0; i < n; ++i) {
            const uint32_t cp = c.u32();
            const uint32_t want = c.u32();
            uint32_t got = 0;
            if (!font.lookup(cp, &got) || got != want)
                out.add("cmap: a sampled codepoint",
                        check::format("U+%04X -> %u, reference %u", cp, got, want));
        }
    }
}

void compare_hmtx(const FaceDigest& face, const OpenTypeFont& font, Findings& out)
{
    if (const golden::Record* r = face.first("HMTX")) {
        H64 h;
        const int64_t n = r->i64(0);
        for (int64_t g = 0; g < n; ++g)
            h.integer(font.advance(static_cast<uint32_t>(g)));
        if (h.hex() != r->str(1))
            out.add("hmtx: advances", "hash over every glyph");
    }
    if (const golden::Record* r = face.first("HMTXS")) {
        golden::Cursor c(*r);
        const int64_t n = c.i64();
        for (int64_t i = 0; i < n; ++i) {
            const uint32_t g = c.u32();
            const int want = c.i32();
            if (font.advance(g) != want)
                out.add("hmtx: a sampled advance",
                        check::format("glyph %u: %d, reference %d", g, font.advance(g), want));
        }
    }
}

void compare_boxes(const FaceDigest& face, const OpenTypeFont& font, Findings& out, Tally& tally)
{
    const golden::Record* r = face.first("BOXES");
    if (r == nullptr)
        return;
    const int64_t n = r->i64(0);
    std::vector<GlyphBounds> boxes(static_cast<size_t>(n));
    for (int64_t g = 0; g < n; ++g)
        boxes[static_cast<size_t>(g)] = font.bounds(static_cast<uint32_t>(g));
    tally.glyphs_boxed += static_cast<long>(n);

    const auto exact = [&](int64_t lo, int64_t hi) {
        H64 h;
        for (int64_t g = lo; g < hi; ++g) {
            const GlyphBounds& b = boxes[static_cast<size_t>(g)];
            h.word(0);
            h.real(b.x_min);
            h.real(b.y_min);
            h.real(b.x_max);
            h.real(b.y_max);
        }
        return h.hex();
    };
    H64 rounded;
    for (const GlyphBounds& b : boxes) {
        rounded.word(0);
        for (double v : {b.x_min, b.y_min, b.x_max, b.y_max})
            rounded.integer(static_cast<int64_t>(py_round(v)));
    }
    const bool exact_ok = exact(0, n) == r->str(1);
    const bool rounded_ok = rounded.hex() == r->str(2);
    if (!rounded_ok || !exact_ok) {
        int bad_chunks = 0;
        if (const golden::Record* chunks = face.first("BOXCHUNKS")) {
            const int64_t k = chunks->i64(0);
            for (int64_t i = 0; i < k; ++i)
                if (exact(i * n / k, (i + 1) * n / k) != chunks->str(static_cast<size_t>(1 + i)))
                    ++bad_chunks;
        }
        out.add(rounded_ok ? "ink boxes: differ below the integer (rounded hash equal)"
                           : "ink boxes: differ by a unit or more (rounded hash differs)",
                check::format("%d of 16 glyph-id chunks differ", bad_chunks));
    }

    for (const golden::Record* s : face.records) {
        const bool integral = s->is("BOXI");
        if (!integral && !s->is("BOXF"))
            continue;
        const uint32_t g = s->u32(0);
        if (g >= boxes.size())
            continue;
        const GlyphBounds& b = boxes[g];
        const double got[4] = {b.x_min, b.y_min, b.x_max, b.y_max};
        for (size_t i = 0; i < 4; ++i) {
            const double want = integral ? static_cast<double>(s->i64(2 + i)) : s->f64(2 + i);
            if (!same_bits(got[i], want)) {
                const std::string kind = s->str(1);
                const bool y = i == 1 || i == 3;
                out.add("ink boxes: a sampled " + kind + " glyph, " + (y ? "y" : "x only"),
                        check::format("glyph %u: (%g, %g, %g, %g), reference field %zu is %g", g,
                                      got[0], got[1], got[2], got[3], i, want));
                if (y)
                    break;
            }
        }
    }
}

void compare_kerning(const FaceDigest& face, const OpenTypeFont& font, Findings& out, Tally& tally)
{
    if (const golden::Record* r = face.first("KERNSUBTABLES")) {
        if (static_cast<int64_t>(font.kern_subtable_count()) != r->i64(0))
            out.add("kern: number of PairPos subtables read",
                    check::format("%zu, reference %lld", font.kern_subtable_count(),
                                  static_cast<long long>(r->i64(0))));
        else
            for (const golden::Record* s : face.records)
                if (s->is("KSUB") &&
                    font.kern_subtable_format(static_cast<size_t>(s->i64(0))) != s->i32(1))
                    out.add("kern: a subtable's format or order", "");
    }
    bool several_lookups = false;
    for (const golden::Record* r : face.records)
        if (r->is("KERNLOOKUPS") && r->i64(2) > 1)
            several_lookups = true;
    tally.accumulate_faces += several_lookups;
    bool differs_here = false;
    for (const golden::Record* r : face.records) {
        if (!r->is("K"))
            continue;
        golden::Cursor c(*r);
        const int64_t n = c.i64();
        for (int64_t i = 0; i < n; ++i) {
            const uint32_t left = c.u32();
            const uint32_t right = c.u32();
            const int want = c.i32();
            const int which = c.i32();
            ++tally.kern_pairs;
            const int got = font.pair_kern(left, right);
            if (several_lookups) {
                const int summed = font.pair_kern_accumulated(left, right);
                ++tally.accumulate_pairs;
                tally.accumulate_nonzero += got != 0 || summed != 0;
                if (summed != got) {
                    ++tally.accumulate_differ;
                    if (!differs_here) {
                        differs_here = true;
                        ++tally.accumulate_differ_faces;
                    }
                    const int by = summed > got ? summed - got : got - summed;
                    if (by > tally.accumulate_worst) {
                        tally.accumulate_worst = by;
                        tally.accumulate_example = check::format(
                            "%s (%u, %u): first value %d, summed %d of %d units "
                            "per em",
                            face.label().c_str(), left, right, got, summed, font.units_per_em());
                    }
                }
            }
            if (got != want)
                out.add(want == 0  ? "kern: a pair the reference does not kern"
                        : got == 0 ? "kern: a pair the reference kerns answers 0"
                                   : "kern: a pair kerned by a different amount",
                        check::format("(%u, %u): %d, reference %d from subtable %d", left, right,
                                      got, want, which));
        }
    }
}

void compare_math(const FaceDigest& face, const OpenTypeFont& font, const FaceData& data,
                  FontLibrary& library, Findings& out)
{
    const golden::Record* consts = face.first("MATHCONSTS");
    if (font.has_math() != (consts != nullptr)) {
        out.add("MATH: presence", font.has_math() ? "read, reference has none" : "not read");
        return;
    }
    if (consts == nullptr)
        return;
    {
        golden::Cursor c(*consts);
        const int64_t n = c.i64();
        for (int64_t i = 0; i < n; ++i) {
            const std::string name = c.str();
            const int want = c.i32();
            int index = -1;
            for (int k = 0; k < MATH_CONSTANT_COUNT; ++k)
                if (name == math_constant_name(k))
                    index = k;
            if (index < 0 || font.math_constant(index) != want)
                out.add("MATH: a constant", name);
        }
    }
    if (const golden::Record* r = face.first("MATHITALICS")) {
        golden::Cursor c(*r);
        const int64_t n = c.i64();
        for (int64_t i = 0; i < n; ++i) {
            const uint32_t g = c.u32();
            const int want = c.i32();
            if (font.math_italic_correction(g) != want)
                out.add("MATH: a sampled italic correction", check::format("glyph %u", g));
        }
    }
    if (const golden::Record* r = face.first("MATHVARIANTS")) {
        H64 h;
        for (uint16_t base : font.math_vertical_bases()) {
            const MathVariantList list = font.math_vertical_variants(base);
            h.word(base);
            h.word(list.size);
            for (const MathVariantRecord& v : list) {
                h.word(v.glyph);
                h.word(v.advance);
                h.word(data.variant_codepoint(v.glyph));
            }
        }
        if (static_cast<int64_t>(font.math_vertical_bases().size()) != r->i64(0))
            out.add("MATH: number of vertical variant lists", "");
        else if (h.hex() != r->str(1))
            out.add("MATH: vertical variant lists or the codepoints their glyphs carry", "");
    }
    for (const golden::Record* r : face.records) {
        if (!r->is("VV"))
            continue;
        golden::Cursor c(*r);
        const uint32_t base = c.u32();
        const int64_t n = c.i64();
        const MathVariantList list = font.math_vertical_variants(base);
        if (static_cast<int64_t>(list.size) != n) {
            out.add("MATH: a sampled variant list's length", check::format("base glyph %u", base));
            continue;
        }
        for (int64_t i = 0; i < n; ++i) {
            const uint32_t glyph = c.u32();
            const uint32_t advance = c.u32();
            const uint32_t codepoint = c.u32();
            const MathVariantRecord& v = list.data[i];
            if (v.glyph != glyph || v.advance != advance)
                out.add("MATH: a sampled variant's glyph or advance",
                        check::format("base glyph %u", base));
            else if (data.variant_codepoint(glyph) != codepoint)
                out.add("MATH: the codepoint a variant glyph carries",
                        check::format("glyph %u: U+%X, reference U+%X", glyph,
                                      data.variant_codepoint(glyph), codepoint));
        }
    }
    if (const golden::Record* r = face.first("SYN")) {
        H64 h;
        for (const SyntheticCodepoint& s : data.synthetic_codepoints()) {
            h.word(s.codepoint);
            h.word(s.glyph);
        }
        if (static_cast<int64_t>(data.synthetic_codepoints().size()) != r->i64(0))
            out.add("MATH: number of minted codepoints",
                    check::format("%zu, reference %lld", data.synthetic_codepoints().size(),
                                  static_cast<long long>(r->i64(0))));
        else if (h.hex() != r->str(1))
            out.add("MATH: which glyph each minted codepoint names (the reference mints in "
                    "glyph-name order)",
                    r->boolean(2) ? "name order is glyph-id order here" : "");
    }
    const golden::Record* params = face.first("MATHPARAMS");
    if (params != nullptr && params->str(0) == "ok") {
        std::string why;
        const MathFontMetrics* metrics = library.math_metrics(params->f64(2), &why);
        if (metrics == nullptr) {
            out.add("MATH: math_metrics refused where the reference answers", why);
            return;
        }
        for (const golden::Record* r : face.records) {
            if (!r->is("MP"))
                continue;
            const std::string name = r->str(0);
            const double* got = math_param(metrics->math(), name);
            if (got == nullptr || !same_bits(*got, r->f64(1)))
                out.add("MATH: a MathParams field", name);
        }
    }
}

// What the BYTES records say a face contains, for the "verified on N" table.
void note_features(const FaceDigest& face, const std::string& label, Tally& tally)
{
    // Once per face, however many records say the same thing.
    std::set<std::string> seen;
    const auto note = [&](const std::string& key) {
        if (seen.insert(key).second)
            Tally::add(tally.verified, key, label);
    };
    if (const golden::Record* r = face.first("SFNT"))
        note("outlines: " + r->str(1));
    if (const golden::Record* r = face.first("HEAD"))
        if (face.first("GLYF") != nullptr)
            note(r->i32(1) != 0 ? "loca: long" : "loca: short");
    if (const golden::Record* r = face.first("POST"))
        note("post: " + r->str(0));
    if (const golden::Record* h = face.first("HHEA"))
        if (const golden::Record* m = face.first("MAXP"))
            if (h->i64(4) < m->i64(0))
                note("hmtx: fewer metrics than glyphs");
    if (const golden::Record* r = face.first("PICK"))
        note("x-height: " + r->str(4));
    for (const golden::Record* r : face.records) {
        if (r->is("CMAPSUB") && r->boolean(5))
            note(check::format("cmap picked: (%d,%d) format %d", r->i32(0), r->i32(1), r->i32(2)));
        if (r->is("CMAPBYTES") && r->i64(3) > 0)
            note("cmap: codepoints other than U+FFFF mapped to glyph 0");
        if (r->is("CMAP") && r->i64(0) > 0 && r->i64(3) > 0)
            note("MATH: minted codepoints");
        if (r->is("KSHAPE") && r->i32(5) == 2) {
            note(check::format("kern: PairPos format %d%s", r->i32(6),
                               r->boolean(4) ? " via Extension" : ""));
            note(check::format("kern: ValueFormat1 0x%04x ValueFormat2 0x%04x", r->i32(7),
                               r->i32(8)));
            note(check::format("kern: Coverage format %d", r->i32(9)));
            if (r->i32(6) == 2)
                note(check::format("kern: ClassDef formats %d and %d", r->i32(10), r->i32(11)));
            if (r->i32(3) != 0)
                note("kern: a lookup flag is set (ignored)");
        }
        if (r->is("KSHAPE") && r->i32(5) != 2)
            note("kern: non-PairPos lookups under the feature (skipped)");
        if (r->is("KERNLOOKUPS") && r->i64(2) > 1)
            note("kern: more than one lookup");
        if (r->is("LEGACYKERN") && r->boolean(0))
            note("legacy kern table present");
        if (r->is("CFF")) {
            note(r->boolean(1) ? "CFF: CID-keyed" : "CFF: name-keyed");
            if (r->i64(5) > 0)
                note("CFF: global subroutines");
            if (r->i64(8) > 0)
                note("CFF: non-integer operands");
            if (!r->boolean(4))
                note("CFF: FontMatrix not the default");
        }
        if (r->is("CFFOPS")) {
            golden::Cursor c(*r);
            const int64_t n = c.i64();
            for (int64_t i = 0; i < n; ++i)
                note("CFF operator: " + c.str());
        }
        if (r->is("GLYF")) {
            golden::Cursor c(*r);
            const int64_t n = c.i64();
            for (int64_t i = 0; i < n; ++i) {
                const std::string key = c.str();
                const int64_t value = c.i64();
                if (key == "max_component_depth")
                    note(
                        check::format("glyf: component depth %lld", static_cast<long long>(value)));
                else if (value > 0 && key != "simple" && key != "components" && key != "no_data")
                    note("glyf: " + key);
            }
        }
        if (r->is("BOXES") && r->i64(4) > 0)
            note("ink boxes: non-integral coordinates");
        if (r->is("MATHCONSTS"))
            note("MATH table");
    }
}

using wild::env_flag;
using wild::sha256_hex;

void print_counts(const char* tag, const std::map<std::string, Count>& counts)
{
    for (const auto& entry : counts)
        std::printf("%s %ld | %s | e.g. %s\n", tag, entry.second.faces, entry.first.c_str(),
                    entry.second.example.c_str());
}

void visit(const FaceDigest& face, std::vector<uint8_t>& bytes, std::string& bytes_path,
           std::string& bytes_sha, Tally& tally)
{
    const std::string label = face.label();
    if (env_flag("IMTYPESET_WILD_TRACE")) {
        std::printf("FACE %s\n", label.c_str());
        std::fflush(stdout);
    }
    ++tally.visited;
    if (bytes_path != face.path) {
        bytes_path = face.path;
        bytes_sha.clear();
        std::FILE* probe = std::fopen(face.path.c_str(), "rb");
        if (probe == nullptr) {
            bytes.clear();
            bytes_sha = "absent";
        } else {
            std::fclose(probe);
            if (!golden::read_file(face.path, bytes))
                bytes_sha = "absent";
        }
        if (bytes_sha.empty() && static_cast<int64_t>(bytes.size()) == face.byte_length)
            bytes_sha = sha256_hex(bytes.data(), bytes.size());
    }
    if (bytes_sha == "absent") {
        ++tally.skipped_absent;
        return;
    }
    if (bytes_sha != face.sha256) {
        ++tally.skipped_changed;
        return;
    }

    // What the reference did with the file as its own API reads it: `stock`
    // for a single font, `indexed` for a face of a collection.
    std::string reference = "ok";
    for (const golden::Record* r : face.records)
        if (r->is("FONTLIB"))
            reference = r->str(2);
    const bool has_math_table = face.first("MATHCONSTS") != nullptr;

    FontLibrary library;
    // The reference reads no 'kern' table, so the comparison is made without
    // the extension that does; that has goldens of its own below.
    library.set_legacy_kern(false);
    const Face role = has_math_table ? Face::MATH : Face::CODE_BOLD;
    std::string why;
    const bool ok =
        library.register_font(role, bytes.data(), bytes.size(), label.c_str(), &why, face.index);
    if (reference != "ok")
        Tally::add(tally.reference_raised,
                   reference + " -> " + (ok ? "loaded" : "refused: " + generalise(why)), label);
    if (!ok) {
        ++tally.refused;
        Tally::add(tally.refusals, generalise(why), label);
        return;
    }
    const FaceData* data = library.face_data(role);
    if (data == nullptr) {
        ++tally.mismatched;
        Tally::add(tally.mismatch_classes, "fontlib: registered but face_data() is null", label);
        return;
    }
    if (reference != "ok") {
        // Nothing to compare against. Touch every glyph so a crash would show.
        for (uint32_t g = 0; g < data->font().glyph_count(); ++g)
            (void)data->font().bounds(g);
        return;
    }
    const OpenTypeFont& font = data->font();
    Findings findings;
    compare_tables(face, font, *data, findings);
    compare_cmap(face, font, findings);
    compare_hmtx(face, font, findings);
    compare_boxes(face, font, findings, tally);
    compare_kerning(face, font, findings, tally);
    compare_math(face, font, *data, library, findings);
    for (const std::string& w : font.warnings())
        Tally::add(tally.warnings, generalise(w), label);

    if (findings.classes.empty()) {
        ++tally.read;
        note_features(face, label, tally);
    } else {
        ++tally.mismatched;
        for (const auto& c : findings.classes)
            Tally::add(tally.mismatch_classes, c.first,
                       c.second.empty() ? label : label + " (" + c.second + ")");
    }
}

} // namespace

TEST(sha256_and_the_digest_hash_are_the_published_functions)
{
    const std::string abc = "abc";
    CHECK_EQ(sha256_hex(reinterpret_cast<const uint8_t*>(abc.data()), abc.size()),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256_hex(nullptr, 0),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    // 56 bytes: the padding spills into a second block.
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK_EQ(sha256_hex(reinterpret_cast<const uint8_t*>(two.data()), two.size()),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    // tools/oracle/dump_font_wild.py: h64([]) and h64([1, -1]).
    CHECK_EQ(H64().hex(), std::string("cbf29ce484222325"));
    H64 h;
    h.integer(1);
    h.integer(-1);
    CHECK_EQ(h.hex(), std::string("f7d0dcf84b177189"));
}

TEST(the_reader_answers_as_the_reference_does_for_every_installed_font)
{
    const std::vector<std::string> files = golden::list("font-wild");
    REQUIRE(!files.empty());

    long stride = env_flag("IMTYPESET_WILD_ALL") ? 1 : WILD_STRIDE;
    if (const char* v = std::getenv("IMTYPESET_WILD_STRIDE"))
        if (std::atol(v) > 0)
            stride = std::atol(v);

    Tally tally;
    std::vector<uint8_t> bytes;
    std::string bytes_path, bytes_sha;
    for (const std::string& path : files) {
        if (path.size() >= 13 && path.compare(path.size() - 13, 13, "/index.golden") == 0)
            continue;
        golden::File file;
        if (!file.load(path))
            continue;
        FaceDigest face;
        bool open = false;
        for (const golden::Record& r : file) {
            if (r.is("FACE")) {
                face = FaceDigest();
                face.path = r.str(0);
                face.index = r.i32(1);
                face.face_count = r.i32(2);
                face.byte_length = r.i64(3);
                face.sha256 = r.str(4);
                open = true;
            } else if (r.is("END") && open) {
                open = false;
                const long ordinal = tally.total++;
                if (ordinal % stride != 0 && face.first("MATHCONSTS") == nullptr) {
                    ++tally.not_sampled;
                    continue;
                }
                visit(face, bytes, bytes_path, bytes_sha, tally);
            } else if (open) {
                face.records.push_back(&r);
            }
        }
    }

    std::printf("WILD faces in the goldens %ld; stride %ld; visited %ld; not sampled %ld\n",
                tally.total, stride, tally.visited, tally.not_sampled);
    std::printf("WILD skipped: file absent %ld; file changed %ld\n", tally.skipped_absent,
                tally.skipped_changed);
    std::printf("WILD read with every compared value equal %ld; refused %ld; MISMATCHED %ld\n",
                tally.read, tally.refused, tally.mismatched);
    std::printf("WILD compared: %ld ink boxes, %ld kern pairs\n", tally.glyphs_boxed,
                tally.kern_pairs);
    std::printf("KERN-ACCUMULATION faces with more than one kern lookup %ld; sampled pairs of "
                "those %ld, of which either rule kerns %ld; pairs where summing the lookups "
                "differs from the first value %ld, on %ld faces; largest difference %d font "
                "units: %s\n",
                tally.accumulate_faces, tally.accumulate_pairs, tally.accumulate_nonzero,
                tally.accumulate_differ, tally.accumulate_differ_faces, tally.accumulate_worst,
                tally.accumulate_example.empty() ? "-" : tally.accumulate_example.c_str());
    print_counts("REFUSED", tally.refusals);
    print_counts("REFERENCE-RAISED", tally.reference_raised);
    print_counts("MISMATCH", tally.mismatch_classes);
    print_counts("WARNING", tally.warnings);
    if (env_flag("IMTYPESET_WILD_FEATURES"))
        print_counts("VERIFIED", tally.verified);
    if (tally.visited == tally.skipped_absent + tally.skipped_changed)
        std::printf("WILD no font of the corpus is present here: nothing was compared\n");

    std::fflush(stdout);

    // A refusal with a reason is allowed. A wrong answer from a font the
    // reader accepted is not; docs/font-support.md lists the classes.
    CHECK_EQ(tally.mismatched, 0L);
}

// The reader's extension beyond the reference: the legacy 'kern' table, as
// fontTools reads it and tools/oracle/dump_kern_legacy.py combines it. Every
// corpus face with such a table is visited; there are few.
TEST(the_legacy_kern_table_is_read_as_fonttools_reads_it_and_used_only_without_gpos)
{
    golden::File file;
    REQUIRE(file.load(golden::path("font-wild-kern/kern.golden")));

    long faces = 0, skipped = 0, compared = 0, used = 0, unused = 0, pairs = 0, tables_unread = 0;
    wild::CorpusFile bytes;
    wild::CorpusFace face;
    bool present = false, readable = false;
    FontLibrary with, without;
    without.set_legacy_kern(false);
    const OpenTypeFont* on = nullptr;
    const OpenTypeFont* off = nullptr;
    int64_t gpos_subtables = -1;
    for (const golden::Record& r : file) {
        CHECK_CONTEXT("%s line %d", face.label().c_str(), r.line);
        if (r.is("FACE")) {
            face = wild::CorpusFace();
            face.path = r.str(0);
            face.index = r.i32(1);
            face.face_count = r.i32(2);
            face.byte_length = r.i64(3);
            face.sha256 = r.str(4);
            ++faces;
            on = off = nullptr;
            gpos_subtables = -1;
            readable = false;
            present = bytes.open(face) == 0;
            if (!present) {
                ++skipped;
                continue;
            }
            std::string why;
            const bool ok =
                with.register_font(Face::PROSE, bytes.bytes.data(), bytes.bytes.size(),
                                   "legacy kern on", &why, face.index) &&
                without.register_font(Face::PROSE, bytes.bytes.data(), bytes.bytes.size(),
                                      "legacy kern off", &why, face.index);
            if (!CHECK(ok))
                continue;
            on = &with.face_data(Face::PROSE)->font();
            off = &without.face_data(Face::PROSE)->font();
            // Turned off, the table is not read at all.
            CHECK(off->legacy_kern_keys().empty());
            CHECK(!off->legacy_kern_used());
        } else if (on == nullptr) {
            continue;
        } else if (r.is("KERNTABLE")) {
            readable = r.str(0) == "ok" && r.i64(1) == 0;
            if (!readable) {
                ++tables_unread;
                CHECK(on->legacy_kern_keys().empty());
            }
        } else if (r.is("GPOSPAIR")) {
            gpos_subtables = r.i64(0);
            CHECK_EQ(static_cast<int64_t>(on->kern_subtable_count()), gpos_subtables);
        } else if (r.is("KPAIRS") && readable) {
            // The whole table: the count, and the hash over every pair.
            H64 h;
            const std::vector<uint32_t>& keys = on->legacy_kern_keys();
            const std::vector<int32_t>& values = on->legacy_kern_values();
            REQUIRE_EQ(keys.size(), values.size());
            for (size_t i = 0; i < keys.size(); ++i) {
                h.word(keys[i] >> 16);
                h.word(keys[i] & 0xFFFFu);
                h.integer(values[i]);
            }
            CHECK_EQ(static_cast<int64_t>(keys.size()), r.i64(0));
            CHECK_EQ(h.hex(), r.str(1));
            ++compared;
            // It answers only for a face whose GPOS gives no pair kerning.
            const bool expect_used = gpos_subtables == 0 && r.i64(0) > 0;
            CHECK_EQ(on->legacy_kern_used(), expect_used);
            ++(expect_used ? used : unused);
        } else if (r.is("KP") && readable) {
            golden::Cursor c(r);
            const int64_t n = c.i64();
            for (int64_t i = 0; i < n; ++i) {
                const uint32_t left = c.u32();
                const uint32_t right = c.u32();
                const int value = c.i32();
                ++pairs;
                if (gpos_subtables == 0) {
                    CHECK_EQ(on->pair_kern(left, right), value);
                    CHECK_EQ(off->pair_kern(left, right), 0);
                } else {
                    // GPOS kerns this face, and the table changes nothing.
                    CHECK_EQ(on->pair_kern(left, right), off->pair_kern(left, right));
                }
            }
        }
    }
    std::printf("LEGACY-KERN faces with a kern table %ld; skipped (file absent or changed) %ld; "
                "tables compared pair for pair %ld; not version 0 or unreadable %ld; used for "
                "kerning (no GPOS pair kerning) %ld; present and not used %ld; sampled pairs "
                "%ld\n",
                faces, skipped, compared, tables_unread, used, unused, pairs);
    std::fflush(stdout);
    REQUIRE(faces > 0);
}

namespace
{

// `font` with the table `tag` replaced by `table`. Checksums are not
// recomputed: the reader does not look at them.
std::vector<uint8_t> with_table_replaced(const std::vector<uint8_t>& font, const std::string& tag,
                                         const std::vector<uint8_t>& table)
{
    const auto u16 = [&](size_t at) {
        return static_cast<uint32_t>((font[at] << 8) | font[at + 1]);
    };
    const auto u32 = [&](size_t at) { return (u16(at) << 16) | u16(at + 2); };
    const auto put32 = [](std::vector<uint8_t>& out, uint32_t v) {
        for (int shift = 24; shift >= 0; shift -= 8)
            out.push_back(static_cast<uint8_t>(v >> shift));
    };
    const size_t count = u16(4);
    std::vector<uint8_t> out(font.begin(), font.begin() + 12), body;
    const size_t data_at = 12 + 16 * count;
    for (size_t i = 0; i < count; ++i) {
        const size_t record = 12 + 16 * i;
        const bool replaced = std::memcmp(font.data() + record, tag.data(), 4) == 0;
        const size_t offset = u32(record + 8), length = replaced ? table.size() : u32(record + 12);
        out.insert(out.end(), font.begin() + static_cast<std::ptrdiff_t>(record),
                   font.begin() + static_cast<std::ptrdiff_t>(record + 8));
        put32(out, static_cast<uint32_t>(data_at + body.size()));
        put32(out, static_cast<uint32_t>(length));
        if (replaced)
            body.insert(body.end(), table.begin(), table.end());
        else
            body.insert(body.end(), font.begin() + static_cast<std::ptrdiff_t>(offset),
                        font.begin() + static_cast<std::ptrdiff_t>(offset + length));
        body.resize((body.size() + 3) & ~size_t(3));
    }
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<uint8_t> from_hex(const std::string& hex)
{
    const auto nibble = [](char c) {
        return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    };
    std::vector<uint8_t> out(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<uint8_t>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
    return out;
}

} // namespace

// What no installed font has as its picked cmap subtable or needs of its post
// table: formats 0, 6 and 13, format 12 groups out of order, which Unicode
// subtable is preferred, and the names of a format 2 post table with unnamed
// and repeated glyphs. tools/oracle/dump_font_synth.py wrote each table by
// hand, put it into a bundled font and recorded how fontTools reads it; the
// same bytes go into the same font here.
TEST(hand_made_cmap_and_post_tables_are_read_as_fonttools_reads_them)
{
    golden::File file;
    REQUIRE(file.load(golden::path("font-synth/synth.golden")));
    std::vector<uint8_t> base;
    REQUIRE(golden::read_file(golden::data_path("fonts/Roboto-Regular.ttf"), base));

    int cases = 0, cmaps = 0, name_tables = 0;
    std::string name, tag;
    std::vector<uint8_t> bytes;
    OpenTypeFont font;
    bool loaded = false;
    std::vector<std::string> names;
    for (const golden::Record& r : file) {
        CHECK_CONTEXT("%s (line %d)", name.c_str(), r.line);
        if (r.is("CASE")) {
            name = r.str(0);
            tag = r.str(1);
            loaded = false;
            ++cases;
        } else if (r.is("TABLE")) {
            bytes = with_table_replaced(base, tag, from_hex(r.str(0)));
            std::string why;
            loaded = font.load(bytes.data(), bytes.size(), &why);
            if (!loaded)
                FAIL("the reader refused the font: %s", why.c_str());
            names.clear();
            if (loaded && tag == "post")
                CHECK(font.glyph_order_names(names));
        } else if (r.is("RAISES")) {
            // fontTools refused it, so the reference would; no case does.
            FAIL("fontTools raised %s and the test has no expectation for that", r.str(0).c_str());
        } else if (!loaded) {
            continue;
        } else if (r.is("PICK")) {
            CHECK_EQ(font.cmap_platform(), r.i32(0));
            CHECK_EQ(font.cmap_encoding(), r.i32(1));
            CHECK_EQ(font.cmap_format(), r.i32(2));
        } else if (r.is("CMAP")) {
            ++cmaps;
            golden::Cursor c(r);
            const int64_t n = c.i64();
            std::vector<std::pair<uint32_t, uint32_t>> want, got;
            for (int64_t i = 0; i < n; ++i) {
                const uint32_t cp = c.u32();
                want.emplace_back(cp, c.u32());
            }
            for (const CmapRange& range : font.cmap())
                for (uint64_t cp = range.first; cp <= range.last; ++cp)
                    got.emplace_back(static_cast<uint32_t>(cp),
                                     range.glyph_of(static_cast<uint32_t>(cp)));
            CHECK_EQ(got.size(), want.size());
            for (size_t i = 0; i < got.size() && i < want.size(); ++i) {
                if (got[i] != want[i]) {
                    FAIL("entry %zu: U+%04X -> %u, fontTools U+%04X -> %u", i, got[i].first,
                         got[i].second, want[i].first, want[i].second);
                    break;
                }
            }
            // And through the lookup, with the codepoint either side of each.
            for (const auto& entry : want) {
                uint32_t glyph = 0;
                CHECK(font.lookup(entry.first, &glyph) && glyph == entry.second);
            }
        } else if (r.is("NAMES")) {
            ++name_tables;
            CHECK_EQ(static_cast<int64_t>(names.size()), r.i64(0));
            H64 h;
            for (const std::string& glyph_name : names) {
                for (const char ch : glyph_name)
                    h.word(static_cast<unsigned char>(ch));
                h.word(0x10000);
            }
            CHECK_EQ(h.hex(), r.str(1));
        } else if (r.is("NAME")) {
            const size_t glyph = static_cast<size_t>(r.i64(0));
            if (CHECK(glyph < names.size()))
                CHECK_EQ(names[glyph], r.str(1));
        } else if (r.is("ORDER")) {
            // The order minting takes unencoded glyphs in: by name, bytewise.
            golden::Cursor c(r);
            const int64_t n = c.i64();
            std::vector<uint32_t> want, got;
            for (int64_t i = 0; i < n; ++i)
                want.push_back(c.u32());
            got = want;
            std::sort(got.begin(), got.end());
            std::stable_sort(got.begin(), got.end(), [&](uint32_t a, uint32_t b) {
                return a < names.size() && b < names.size() && names[a] < names[b];
            });
            CHECK(got == want);
        }
    }
    std::printf("SYNTH cases %d: cmap tables compared entry for entry %d, post tables compared "
                "name for name %d\n",
                cases, cmaps, name_tables);
    CHECK(cases >= 11);
}
