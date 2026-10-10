// The style subset: font keys, the adapter between font metrics and the layout
// core, TeX's space factor and interword glue, the strut, and the composition
// that turns a run of styled text into horizontal-list material. Mirrors what
// the native side needs of advtext/style.py, with the text composition of
// advtext/markdown.py (`_Converter._append_text`, `_stand_in`, `_fill_and_break`
// and the run-building half of `text_block`), which is where the reference
// keeps it.
//
// Not here: themes, block kinds and block styles, the type scale, emphasis,
// markdown. A caller says which face at which size and how sentence spaces are
// treated; which face a heading takes is the host's or a binding's decision.
//
// TEXT IS UTF-8, AND UNTRUSTED. The reference is handed Python strings; this
// is handed bytes. `decode_utf8` has the policy: a malformed sequence becomes
// U+FFFD, one per maximal ill-formed subsequence, and is from then on a
// U+FFFD the text contained. Nothing read from a string indexes anything.
//
// CHARACTER CLASSES ARE PYTHON'S, from src/unicode_tables.h: white space is
// `str.isspace()` (so U+00A0 is an interword space and a breakpoint, as it is
// in the reference), upper case is `str.isupper()`.

#pragma once

#include "fontmetrics.h"
#include "nodes.h"
#include "pack.h"
#include "tex_rules.h"
#include "units.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ImTypeset::detail
{

class FontLibrary;

// --- Constants ----------------------------------------------------------------------

// Interword stretch and shrink as fractions of the space's width: the
// proportions of cmr10's \fontdimen 3 and 4 to its \fontdimen 2, which an
// OpenType face does not state.
constexpr double SPACE_STRETCH_FRACTION = 0.5;
constexpr double SPACE_SHRINK_FRACTION = 1.0 / 3.0;
// \fontdimen 7, extra_space, likewise.
constexpr double EXTRA_SPACE_FRACTION = 1.0 / 3.0;

// The space factor that leaves a space as the font has it (tex.web section 1034).
constexpr int NEUTRAL_SPACE_FACTOR = 1000;

// How much of a strut sits above the baseline; the rest sits below it.
constexpr double STRUT_HEIGHT_FRACTION = 0.75;

// U+FFFD: what a malformed byte sequence decodes to, and what is set in place
// of a character no face has.
constexpr uint32_t REPLACEMENT_CHARACTER = 0xFFFD;

// The glue that ends a paragraph (tex.web section 816): one fil of stretch.
constexpr GlueSpec PARFILLSKIP = {0, UNITY, GlueOrder::FIL, 0, GlueOrder::NORMAL};

// How much of tex.web section 1044's sentence-space rule a document applies.
// Section 1044 does three things to the glue after a space factor of 2000 or
// more: adds extra_space to its width, multiplies its stretch by the factor
// and divides its shrink by it (the last two for any factor but 1000).
enum class Spacing : uint8_t {
    // Section 1044 entire: plain TeX's default.
    NONFRENCH = 0,
    // \frenchspacing: every interword space identical.
    FRENCH,
    // The widening without the stretch and shrink scaling. This has no
    // counterpart in tex.web; it is the reference theme's default.
    EXTRA_SPACE_ONLY,
};

// How a line's slack is disposed of once the breaker has chosen where to cut.
enum class Align : uint8_t { JUSTIFY = 0, LEFT, CENTER, RIGHT };

// --- Font keys ----------------------------------------------------------------------

// A face resolved at a size: what a FontKey names. `size` is the em size in
// scaled points.
struct FontSpec {
    Face face = Face::PROSE;
    Scaled size = 0;

    // The key CHAR nodes carry. The size must be within [0, FontKey::MAX_SIZE].
    FontKey key() const;
};

// The key for a face at a size: the face id of a FontKey is the Face's value.
// A size outside [0, FontKey::MAX_SIZE] is a contract violation and is clamped
// (FontKey::make).
FontKey font_key(Face face, Scaled size);

// The inverse. False, leaving `out` alone, for a key whose face id is not one
// of the seven Faces: a key minted by something else.
bool parse_font_key(FontKey key, FontSpec* out);

// The reference's spelling of a key, "prose-bold@789381", for messages and for
// reading goldens. Any size is written, a negative one included.
std::string font_key_text(Face face, Scaled size);

// Reads that spelling: split at the last '@', a face name before it and a
// decimal integer after. The integer is read as Python's int() reads ASCII:
// an optional sign, digits with single underscores allowed between them, and
// ASCII white space around the whole. False for anything else, for a size that
// does not fit 64 bits, and for digits outside ASCII, which the reference
// accepts and this does not.
bool parse_font_key_text(std::string_view text, FontSpec* out);

// --- Metrics --------------------------------------------------------------------------

// How a key becomes measurable: a function and the context it wants back.
// Injected so a test can supply a fake with round numbers and never open a
// font. Null means the resolver does not know the key's face.
struct MetricsResolver {
    using Function = const FontMetrics* (*)(void* user, FontKey key);

    Function function = nullptr;
    void* user = nullptr;

    explicit operator bool() const { return function != nullptr; }
    const FontMetrics* operator()(FontKey key) const
    {
        return function != nullptr ? function(user, key) : nullptr;
    }
};

// The production resolver: a key's face at a key's size from a FontLibrary.
// The size, in scaled points, is handed over as the em size, so every length
// that comes back is in scaled points, as a double. The library must outlive
// the resolver.
MetricsResolver fontlib_metrics(FontLibrary& library);

// The hpack adapter: a character in, scaled-point dimensions out. This is the
// one rounding between the metrics layer and the layout core — round() of the
// advance and of the ink box's height and depth, half to even, with neither
// height nor depth clamped at zero.
//
// Memoised per (key, codepoint): hpack measures every character on every pass
// and the line breaker packs a paragraph many times over. The memo is not
// repaired when a font registry changes (`clear` drops it), which is the
// reference's behaviour too.
//
// A key the resolver does not know measures as nothing, {0, 0, 0}, where the
// reference raises whatever its resolver raised.
class CharMeasureCache
{
public:
    explicit CharMeasureCache(MetricsResolver resolve);

    CharDimens operator()(FontKey font, uint32_t codepoint);

    // What hpack and ship-out take. Valid while this object is alive and not moved.
    CharMeasure measure();

    void clear();
    size_t size() const { return _count; }

private:
    struct Slot {
        uint64_t font = 0;
        uint32_t codepoint = 0;
        bool used = false;
        CharDimens dimens;
    };

    static size_t hash(uint64_t font, uint32_t codepoint);
    void grow();

    MetricsResolver _resolve;
    std::vector<Slot> _slots; // open addressing; the size is a power of two
    size_t _count = 0;
    // The last key resolved: a run of text is one key many times over.
    uint64_t _last_font = ~uint64_t(0);
    const FontMetrics* _last_metrics = nullptr;
};

// --- Space factor and interword glue ------------------------------------------------

// The space factor after setting one character: tex.web's adjust_space_factor
// (section 1034), a macro there and a function of the reference's name here.
// The \sfcode classes are the reference's four: 3000 after `.`, `?`, `!`; 0
// (transparent, the factor is left alone) after `)`, `]`, `'`, `"`, U+201D and
// U+2019; 999 after an upper-case character; 1000 after anything else. A
// sentence-ending mark raises the factor to 3000 only when it was not already
// below 1000, so the period after "NASA" leaves an ordinary space.
int space_factor_after(uint32_t codepoint, int current = NEUTRAL_SPACE_FACTOR);

// The glue between two words, scaled by the space factor: what tex.web's
// app_space makes (section 1043, with sections 1042 and 1044 inside it), less
// its space_skip and xspace_skip overrides, which are not here. The name is
// the reference's. `space_width` is the face's advance for U+0020, rounded.
//
// The default here is NONFRENCH, the transcription of section 1044, as it is
// for the reference's function; Composition's default is EXTRA_SPACE_ONLY, as
// it is for the reference's Theme. A space factor below 1 is a contract
// violation and is treated as 1000.
//
// `rules` says how section 1044's two quotients are rounded: half to even, the
// reference's, unless TexRules::sentence_space_truncates.
GlueSpec interword_glue(Scaled space_width, int space_factor = NEUTRAL_SPACE_FACTOR,
                        Spacing spacing = Spacing::NONFRENCH, TexRules rules = TexRules());

// --- Strut ------------------------------------------------------------------------------

// TeX's \strut as data: the height and depth no line of a block may fall
// below, measured from the baseline, upward and downward.
struct Strut {
    Scaled height = 0;
    Scaled depth = 0;

    // The strut that exactly fills one baseline-to-baseline distance. The
    // depth is the remainder and not a second rounding, so height + depth is
    // `leading` to the scaled point and the interline glue of tex.web section
    // 679 between two strutted lines is exactly zero.
    static Strut filling(Scaled leading);

    Scaled total() const { return height + depth; }

    // `box` with the strut in it: its own height and depth, floored at the
    // strut's. The same two numbers a repack with a zero-width strut box in
    // the list would reach; the glue setting is untouched.
    void apply_to(Box& box) const;
};

// --- Text ---------------------------------------------------------------------------------

// Appends the codepoints of `text` to `out`. Well-formed UTF-8 decodes to what
// it encodes. Anything else — a stray continuation byte, a truncated sequence,
// an overlong form, an encoded surrogate, a value above U+10FFFF, the bytes
// 0xC0, 0xC1 and 0xF5..0xFF — becomes U+FFFD, one for each maximal subpart of
// an ill-formed subsequence (the Unicode Standard's recommended practice, and
// what Python's `bytes.decode("utf-8", "replace")` does). A NUL byte is U+0000
// and does not end the text. If `offsets` is given, the byte offset each
// codepoint started at is appended to it.
void decode_utf8(std::string_view text, std::vector<uint32_t>& out,
                 std::vector<uint32_t>* offsets = nullptr);

// What composition needs of the reference's Theme.
struct Composition {
    Spacing spacing = Spacing::EXTRA_SPACE_ONLY;
    // Which of the reference's departures from tex.web are kept in the glue
    // between words: all of them unless said otherwise (src/tex_rules.h).
    TexRules rules;
    // The faces a character its own face lacks is looked for in, with MATH
    // between the two: the reference's `Faces.regular` and `Faces.code`.
    Face regular = Face::PROSE;
    Face code = Face::CODE;
};

// What to set for a character the face of `key` has no glyph for: the same
// character from the first of the regular face, the math face and the code
// face that has it, at the same size; failing that U+FFFD from the regular
// face, or `?` if it lacks that too. White space and the general categories
// Cc, Cf, Mn and Me are never stood in for, and are answered unchanged, as is
// everything when no stand-in exists. A face the resolver does not know is
// passed over.
struct StandIn {
    FontKey font;
    uint32_t codepoint = 0;
};
StandIn stand_in(const Composition& composition, MetricsResolver resolve, FontKey key,
                 uint32_t codepoint);

// Material under construction. The space factor carries from one append to
// the next, so runs in several faces make one sentence.
struct Run {
    std::vector<Node> nodes;
    int space_factor = NEUTRAL_SPACE_FACTOR;

    // If not null, one entry is appended for every node appended to `nodes`
    // by the functions below: the byte offset, in the text that was passed, of
    // the character the node comes from. A CHAR is its character; an interword
    // GLUE, and the penalty before it in an unbreakable run, the first
    // character of the white space it replaces; a KERN the character it
    // follows; the nodes of a hard break the newline. `source_base` is added
    // to each.
    std::vector<uint32_t>* source_offsets = nullptr;
    uint32_t source_base = 0;

    // Scratch, kept so repeated appends do not allocate.
    std::vector<uint32_t> codepoints;
    std::vector<uint32_t> offsets;
};

// Appends one styled run of text as characters, kerns and interword glue.
//
//   - A run of white space is one interword glue, built from the face's space
//     width and the run's space factor. White space with nothing before it in
//     `run.nodes` is dropped.
//   - With `breakable` false the glue is preceded by a penalty of INF_PENALTY,
//     so the line breaker cannot cut there (an inline code span).
//   - A character the face lacks is set as `stand_in` says; one replaced by
//     another character is a replaced_char node that remembers what was
//     written.
//   - A pair kern follows a character only when its own face has it, a
//     character follows it in this same text, and that character is not white
//     space; it is rounded half to even and a zero kern is not emitted.
//
// False, with a reason and nothing appended, if the resolver does not know
// the font's face.
bool append_text(Run& run, std::string_view text, FontSpec font, bool breakable,
                 const Composition& composition, MetricsResolver resolve,
                 std::string* why = nullptr);

// The paragraph terminator of tex.web section 816 — a penalty forbidding a
// break, the parfillskip glue, a forced break — appended to `nodes`; also what
// a hard line break within a paragraph is. In a block set ragged left or
// centred the fill is left out and the forced break stands alone: those
// alignments put one fil of \leftskip on every line, and a second infinite
// glue would share the slack with it.
void fill_and_break(Align align, std::vector<Node>& nodes);

// Plain text as the material of one block, set exactly as written: the run a
// reference `text_block` builds, before the first-line indent and the marker,
// which belong to paragraph assembly.
//
// `\r\n` and a lone `\r` are newlines. By default every run of white space,
// newlines included, is one interword space, and white space at either end is
// dropped. With `hard_breaks` each newline ends the line there
// (`fill_and_break`) and the text carries on below; white space either side of
// a newline is dropped, as are newlines at the very start and end, and a blank
// line in the middle sets a blank line.
bool append_text_block(Run& run, std::string_view text, FontSpec font, bool hard_breaks,
                       Align align, const Composition& composition, MetricsResolver resolve,
                       std::string* why = nullptr);

} // namespace ImTypeset::detail
