// A `$...$` subset, turned into the atom vocabulary of src/math_atoms.h.
// Mirrors advtext/mathtext.py: a tokenizer and a recursive descent over a fixed
// table. It is deliberately not TeX's mouth: no macros, no catcodes, no state.
//
// THE CONTRACT: a formula, or a refusal, and never a guess. `parse` is total:
// for every byte string it answers a math list or a Refusal, and a Refusal
// carries a kind a host can branch on, one sentence a person can read, and
// where in the source the offending thing starts. There is no best-effort
// path: a formula that sets with a piece silently missing reads as a different
// formula, and the caller's fallback — the source, set as code — is honest
// where a guess is not.
//
// THE REASONS ARE THE REFERENCE'S, character for character, so that a host in
// any language shows the same sentence. Some embed Python's spelling of a
// character (`repr`); `python_repr` in src/math_mlist.h writes it.
//
// WHAT IS IN THE SUBSET is what src/math_mlist builds: scripts and primes,
// \frac and \binom, big operators with \limits and \nolimits, \overline and
// \underline, \left ... \right over the delimiters that have a chain of sizes,
// \sqrt without a degree, the explicit spaces, \text and its relations, the
// \mathrm, \operatorname, \mathbb, \mathbf and \mathcal letters, and the symbols
// of the tables in src/mathtext_tables.inc, which are generated from the
// reference's own (tools/gen_mathtext_tables.py). Accents, style changes,
// matrices, alignment, infix fractions and macros are refused, each with its
// own kind.
//
// Four choices a reader comparing this with LaTeX should know, all the
// reference's: a \frac is wrapped in an Inner atom; integrals keep their
// bounds beside them and sums take the engine's default; variables are math
// italic, digits and capital Greek upright; \dots is \ldots.
//
// THE SOURCE IS UTF-8, AND UNTRUSTED. It is decoded once (`decode_utf8`:
// a malformed sequence is U+FFFD, which is then a character outside the
// subset) and parsed as codepoints, as the reference parses a Python string.
// Nesting of braces, scripts and arguments is bounded by MAX_DEPTH, which
// bounds this parser's recursion and, through the list it produces,
// mlist_to_hlist's. The pass is linear: one left-to-right scan, no
// backtracking. Its length is bounded by MAX_SOURCE_BYTES, which bounds what
// the pass allocates; the reference reads a source of any length.
//
// POSITIONS. The reference reports an offset in codepoints. A host holding
// UTF-8 wants bytes. A Refusal has both.
//
// Not here: where a `$` opens a formula in running text, which is the
// reference's markdown rule, and the memo of built formulas
// (`markdown_math`), which belongs to whoever owns the document.

#pragma once

#include "math_atoms.h"
#include "math_mlist.h"
#include "nodes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ImTypeset::detail
{

// How deeply braces, scripts and arguments may nest before `parse` refuses. A
// stack guard, not a typographic limit: four levels already reach the
// scriptscript floor. A level here is at most two levels of list (a \frac
// taken as an argument), and the innermost argument may be a list of its own
// (an operator name is one), so the lists `parse` lets through nest at most
// 2 * MAX_DEPTH + 2 deep, which is inside MAX_MATH_NESTING.
constexpr int MAX_DEPTH = 24;

// The longest source `parse` reads, in bytes; a longer one is refused, kind
// LAYOUT, before any of it is decoded. The reference has no bound. This one is
// there because the pass asks for about two hundred bytes of heap for each
// byte of source (208 for a run of `x`, measured), so that what a formula can
// cost is a stated amount and not whatever arrived between two dollars. It is
// thousands of times the longest formula a person writes, and no smaller
// because the reference's own table of hostile sources, which runs to 450,000
// bytes, is read here at its real sizes and answered as the reference answers.
constexpr size_t MAX_SOURCE_BYTES = size_t(1) << 19;

// Why a source was not turned into a formula; one per class of reason. FONT
// and LAYOUT are `typeset`'s alone, because `parse` has no font.
enum class RefusalKind : uint8_t {
    EMPTY = 0,
    SYNTAX,
    NESTING,
    UNKNOWN_COMMAND,
    CHARACTER,
    MACRO,
    ENVIRONMENT,
    MATRIX,
    ALIGNMENT,
    INFIX,
    DELIMITER,
    RADICAL,
    ACCENT,
    STYLE,
    SPACING,
    TEXT,
    FONT,
    LAYOUT,
};

constexpr int REFUSAL_KIND_COUNT = 18;

// The reference's value for a kind: "empty", "unknown-command", "layout".
const char* refusal_kind_value(RefusalKind kind);

// A source outside the subset: why, and where. `position` is the start of the
// command or character that could not be taken, as an offset into the source
// in codepoints (the reference's), and `byte_position` the same place in
// bytes. Both are within the source, its end included.
struct Refusal {
    RefusalKind kind = RefusalKind::EMPTY;
    std::string reason;
    size_t position = 0;
    size_t byte_position = 0;
};

// A source the subset covers, as a math list: `items` is what mlist_to_hlist
// consumes, and the sublists and prose it names are in `list`. `display` is
// carried, not baked in: the atoms are the same in both styles.
struct Formula {
    MathList list;
    MathSpan items;
    bool display = false;

    // DISPLAY for `$$...$$`, TEXT for `$...$`.
    Style style() const { return display ? Style::DISPLAY : Style::TEXT; }

    // The top-level items. Valid until `list` is next added to.
    MathView view() const
    {
        MathView out;
        list.view(items, &out);
        return out;
    }
};

// Turns the text between the dollars into a math list, or says why not. True
// with `formula` filled, or false with `refusal` filled; the other is left
// empty. `display` only chooses the style the result is set at. Storage held by
// `formula` is reused, so a caller parsing many sources keeps one.
bool parse(std::string_view source, bool display, Formula& formula, Refusal& refusal);

// One formula as a packed hbox at its natural width, on its baseline: a BOX
// node of `arena`, to be shipped with `fonts.measure()`. False, with nothing
// left in `arena`, if the fonts in hand cannot set it; `status` then says why.
// A box with a dimension outside +-MAX_DIMEN is TOO_LARGE and is not stored:
// no node may hold one (src/units.h), and a row of explicit spaces reaches it.
bool set_formula(const Formula& formula, MathFonts& fonts, NodeArena& arena, Node& box,
                 MathStatus& status);

// What a host is told when the engine would not set a formula that parsed:
// the reference's `typeset` turns its exceptions into these.
//
//   DELIMITER_TOO_SMALL    LAYOUT, position 0: what is under the root, or
//                          between the delimiters, needs one so many times as
//                          tall as the largest the math face has.
//   MISSING_GLYPH          FONT, at the first place the character occurs in
//                          the source (0 if it does not occur as itself).
//   TEXT_FACE_UNAVAILABLE  FONT, position 0.
//   TOO_LARGE, TOO_DEEP    LAYOUT, position 0: "the formula is too large to
//                          set: " and math_error_message. Not the font's
//                          doing, and not the reference's: it has neither
//                          bound. A source `parse` reads can reach TOO_LARGE
//                          by its box (a row of \qquad wider than MAX_DIMEN).
//   anything else          FONT, position 0, with math_error_message. The
//                          parser emits no list that reaches these.
//
// The proportion in the first is written with a decimal point whatever
// LC_NUMERIC is: a reason is the same sentence on every machine.
//
// The first two reasons are the reference's to the character. So is the third
// when there is no text resolver at all; when there is one and it does not know
// the face, the reference's reason ends with whatever its resolver raised, and
// this one says only that the face did not resolve.
Refusal refusal_from(const MathStatus& status, std::string_view source, const Formula& formula,
                     const MathFonts& fonts);

// A reason as a C string. A reason quotes what the source held, and a source
// may hold U+0000, which is a character like any other to the parser and to
// the reference, and the end of the text to whoever reads a `const char*`.
// Each is written here as Python writes it elsewhere in a reason: the four
// characters \x00. Everything else is left as it is.
std::string c_string_reason(std::string_view reason);

// `parse` and `set_formula` as one call. True with `formula` and `box` filled;
// false with `refusal` filled, by the parser or by `refusal_from`.
bool typeset(std::string_view source, bool display, MathFonts& fonts, NodeArena& arena,
             Formula& formula, Node& box, Refusal& refusal);

// --- The tables, for a page that lists the subset and for the tests ---------------

// Every command name `parse` accepts, without the backslash, sorted as bytes.
// A one-character name is a control symbol: "{" is \{.
size_t supported_command_count();
const char* supported_command(size_t index);

// Every command `parse` recognises in order to refuse it, sorted, with the kind
// it gives and the rest of its reason, which is "\name " followed by this.
struct RefusedCommand {
    const char* name = "";
    RefusalKind kind = RefusalKind::EMPTY;
    const char* reason_tail = "";
};
size_t refused_command_count();
RefusedCommand refused_command(size_t index);

// What may follow \left or \right, as it is written — a bare character ("(")
// or a command with its backslash ("\langle") — and the codepoint sized. -1 is
// the null delimiter ".", which sets nothing and still closes the pair.
struct SizedDelimiter {
    const char* written = "";
    int32_t codepoint = -1;
};
size_t sized_delimiter_count();
SizedDelimiter sized_delimiter(size_t index);

// The commands that set words inside a formula, and the prose face each uses.
struct ProseCommand {
    const char* name = "";
    Face face = Face::PROSE;
};
size_t prose_command_count();
ProseCommand prose_command(size_t index);

} // namespace ImTypeset::detail
