// See mathtext.h. The parser below is the reference's `_Parser`, method for
// method and branch for branch, because the order of its tests decides which
// reason a source is refused with and the goldens hold every one.
//
// Where the reference raises `_Refused` and unwinds, a method here returns
// false with the refusal recorded, and every caller returns it on.
//
// Lists under construction live on one stack, `_stack`, as in math_mlist.cpp:
// a method that "returns items" pushes them and its caller reads the run from
// where the stack stood.

#include "mathtext.h"

#include "pack.h"
#include "style.h"
#include "unicode_tables.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ImTypeset::detail
{

namespace
{

struct SymbolEntry {
    const char* name;
    AtomType kind;
    Limits limits;
    uint32_t codepoint;
    // If not null the nucleus is these characters as upright ordinary atoms
    // (an operator name) and `codepoint` means nothing.
    const char* upright;
};

struct CharacterEntry {
    uint32_t character;
    AtomType kind;
    Limits limits;
    uint32_t codepoint;
};

struct SpaceEntry {
    const char* name;
    int width;
    int stretch;
    int shrink;
};

struct ProseEntry {
    const char* name;
    Face face;
};

struct DelimiterCharacterEntry {
    uint32_t character;
    uint32_t codepoint;
    bool null;
};

struct DelimiterCommandEntry {
    const char* name;
    uint32_t codepoint;
};

struct RefusedEntry {
    const char* name;
    RefusalKind kind;
    const char* reason;
};

struct SizedDelimiterEntry {
    const char* written;
    int32_t codepoint;
};

struct ItalicRange {
    uint32_t first;
    uint32_t last;
    uint32_t target;
};

struct CodepointPair {
    uint32_t key;
    uint32_t value;
};

#include "mathtext_tables.inc"

static_assert(TABLE_MAX_DEPTH == MAX_DEPTH, "MAX_DEPTH is the reference's");
// A level of the parser's nesting is at most two of the list's: a \frac taken
// as an argument is an Inner atom holding a list holding the fraction, whose
// numerator is a list again.
static_assert(2 * MAX_DEPTH + 2 <= MAX_MATH_NESTING, "a list the parser lets through must convert");
static_assert(MAX_SOURCE_BYTES < 0x7FFFFFFF, "positions in a source are held in 32 bits");

const char* name_of(const char* entry)
{
    return entry;
}

template<typename Entry>
const char* name_of(const Entry& entry)
{
    return entry.name;
}

// A table sorted by name, searched by name.
template<typename Entry, size_t N>
const Entry* find_named(const Entry (&table)[N], std::string_view name)
{
    size_t low = 0;
    size_t high = N;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        const int order = std::string_view(name_of(table[middle])).compare(name);
        if (order == 0)
            return &table[middle];
        if (order < 0)
            low = middle + 1;
        else
            high = middle;
    }
    return nullptr;
}

template<size_t N>
const CharacterEntry* find_character(const CharacterEntry (&table)[N], uint32_t character)
{
    size_t low = 0;
    size_t high = N;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (table[middle].character == character)
            return &table[middle];
        if (table[middle].character < character)
            low = middle + 1;
        else
            high = middle;
    }
    return nullptr;
}

template<size_t N>
bool find_pair(const CodepointPair (&table)[N], uint32_t key, uint32_t* value)
{
    for (const CodepointPair& pair : table) {
        if (pair.key == key) {
            *value = pair.value;
            return true;
        }
    }
    return false;
}

bool ascii_alpha(long c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool ascii_digit(long c)
{
    return c >= '0' && c <= '9';
}

// Variables are math italic: the letter's place in Unicode's mathematical
// italic alphabets, with the letters whose italic form is encoded elsewhere
// (U+210E for h, because U+1D455 does not exist).
uint32_t italic(uint32_t codepoint)
{
    uint32_t found = 0;
    if (find_pair(ITALIC_EXCEPTIONS, codepoint, &found))
        return found;
    for (const ItalicRange& range : ITALIC_RANGES) {
        if (codepoint >= range.first && codepoint <= range.last)
            return range.target + codepoint - range.first;
    }
    return codepoint;
}

// One letter in \mathbb, \mathbf or \mathcal; false if it has no form there.
// Unicode's mathematical alphanumerics, with the holes the Letterlike Symbols
// block had already filled. \mathbb and \mathcal are capitals only; \mathbf
// takes letters and digits.
bool alphabet_form(std::string_view command, uint32_t character, uint32_t* out)
{
    if (character >= 0x80)
        return false;
    const bool upper = character >= 'A' && character <= 'Z';
    const uint32_t offset = character - 'A';
    if (command == "mathbb") {
        if (!upper)
            return false;
        if (!find_pair(BLACKBOARD_HOLES, character, out))
            *out = 0x1D538 + offset;
        return true;
    }
    if (command == "mathcal") {
        if (!upper)
            return false;
        if (!find_pair(SCRIPT_HOLES, character, out))
            *out = 0x1D49C + offset;
        return true;
    }
    if (upper) {
        *out = 0x1D400 + offset;
        return true;
    }
    if (character >= 'a' && character <= 'z') {
        *out = 0x1D41A + (character - 'a');
        return true;
    }
    if (character >= '0' && character <= '9') {
        *out = 0x1D7CE + (character - '0');
        return true;
    }
    return false;
}

void mathtext_append_utf8(std::string& out, uint32_t codepoint)
{
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

std::string mathtext_formatted(const char* format, ...) IM_FMTARGS(1);

std::string mathtext_formatted(const char* format, ...)
{
    char buffer[128];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    if (written <= 0)
        return std::string();
    const size_t length = static_cast<size_t>(written);
    return std::string(buffer, length < sizeof buffer ? length : sizeof buffer - 1);
}

MathItem character_atom(AtomType kind, uint32_t codepoint, Limits limits = Limits::DEFAULT)
{
    return MathItem::atom(kind, Field::math_char(codepoint), Field(), Field(), limits);
}

// One pass over one source. Scannerless: `_index` is the only state but depth.
class Parser
{
public:
    Parser(const std::vector<uint32_t>& source, MathList& list)
        : _source(source)
        , _list(list)
    {
    }

    // The whole source as a list, added to the MathList. False: refused.
    bool formula(MathSpan& items)
    {
        if (!this->items(nullptr, nullptr))
            return false;
        if (_stack.empty())
            return refuse(RefusalKind::EMPTY, "there is nothing between the dollars", 0);
        items = _list.add_items(_stack);
        return true;
    }

    RefusalKind kind() const { return _kind; }
    const std::string& reason() const { return _reason; }
    size_t position() const { return _position; }

private:
    static constexpr long END = -1;

    bool refuse(RefusalKind kind, std::string reason, size_t position)
    {
        _kind = kind;
        _reason = std::move(reason);
        _position = position;
        return false;
    }

    // ---- scanning

    void skip_blanks()
    {
        while (_index < _source.size() && py_isspace(_source[_index]))
            ++_index;
    }

    // The next non-blank character, or END. Consumes the blanks. U+0000 is a
    // character like any other.
    long peek()
    {
        skip_blanks();
        return _index < _source.size() ? static_cast<long>(_source[_index]) : END;
    }

    std::string text(size_t first, size_t last) const
    {
        std::string out;
        for (size_t i = first; i < last && i < _source.size(); ++i)
            mathtext_append_utf8(out, _source[i]);
        return out;
    }

    // Reads `\name` or `\x` at `_index`, which is on the backslash: a run of
    // ASCII letters, or failing that exactly one character of any kind.
    bool command_name(std::string& name)
    {
        const size_t start = _index + 1;
        if (start >= _source.size())
            return refuse(RefusalKind::SYNTAX, "a backslash with nothing after it", _index);
        size_t end = start;
        while (end < _source.size() && ascii_alpha(static_cast<long>(_source[end])))
            ++end;
        if (end == start)
            end = start + 1;
        _index = end;
        name = text(start, end);
        _name_is_blank = end == start + 1 && py_isspace(_source[start]);
        return true;
    }

    bool enter(size_t position)
    {
        ++_depth;
        if (_depth > MAX_DEPTH) {
            return refuse(RefusalKind::NESTING,
                          mathtext_formatted("nested more than %d levels deep", MAX_DEPTH),
                          position);
        }
        return true;
    }

    // Whether the command at `position` is exactly `\name`, and not `\namesake`.
    bool at_command(const char* name, size_t position) const
    {
        const size_t length = std::strlen(name);
        if (position + 1 + length > _source.size())
            return false;
        for (size_t i = 0; i < length; ++i) {
            if (_source[position + 1 + i] !=
                static_cast<uint32_t>(static_cast<unsigned char>(name[i])))
                return false;
        }
        const size_t end = position + 1 + length;
        return !(end < _source.size() && ascii_alpha(static_cast<long>(_source[end])));
    }

    // Whether the source continues with these ASCII characters from `position`.
    bool starts_with(const char* what, size_t position) const
    {
        const size_t length = std::strlen(what);
        if (position + length > _source.size())
            return false;
        for (size_t i = 0; i < length; ++i) {
            if (_source[position + i] != static_cast<uint32_t>(static_cast<unsigned char>(what[i])))
                return false;
        }
        return true;
    }

    // The first '}' at or after `start`, or the length of the source.
    size_t find_close(size_t start) const
    {
        size_t end = start;
        while (end < _source.size() && _source[end] != '}')
            ++end;
        return end;
    }

    // ---- lists

    // Pushes a list of atoms up to the end, or up to the `}` matching
    // `opened_at`, or — when `left_at` is the position of a \left — up to its
    // \right. The `}` is consumed; the \right is not, because its delimiter
    // follows it and left_right reads the two together.
    bool items(const size_t* opened_at, const size_t* left_at)
    {
        for (;;) {
            const long character = peek();
            const size_t position = _index;
            if (character == END) {
                if (left_at != nullptr) {
                    return refuse(RefusalKind::SYNTAX, "a \\left with no \\right after it",
                                  *left_at);
                }
                if (opened_at != nullptr)
                    return refuse(RefusalKind::SYNTAX, "a { that is never closed", *opened_at);
                return true;
            }
            if (character == '}') {
                if (left_at != nullptr) {
                    return refuse(RefusalKind::SYNTAX, "a } between a \\left and its \\right",
                                  position);
                }
                if (opened_at == nullptr)
                    return refuse(RefusalKind::SYNTAX, "a } with no { before it", position);
                ++_index;
                return true;
            }
            if (character == '\\' && at_command("right", position)) {
                if (left_at == nullptr) {
                    return refuse(RefusalKind::SYNTAX, "a \\right with no \\left before it",
                                  position);
                }
                return true;
            }
            MathItem parsed;
            if (character == '^' || character == '_' || character == '\'') {
                // TeX's rule: a script with nothing to hang on hangs on nothing.
                if (!scripts(MathItem::atom(AtomType::ORD), parsed))
                    return false;
                _stack.push_back(parsed);
                continue;
            }
            if (!atom(parsed))
                return false;
            // A space is glue, not a noad: a script after one hangs on nothing
            // (the branch above, next time round), not on the space.
            if (parsed.kind != AtomType::SPACE) {
                MathItem scripted;
                if (!scripts(parsed, scripted))
                    return false;
                parsed = scripted;
            }
            _stack.push_back(parsed);
        }
    }

    // `{ ... }` at `_index`, which is on the brace. Pushes its items.
    bool group()
    {
        const size_t opened_at = _index;
        if (!enter(opened_at))
            return false;
        ++_index;
        if (!items(&opened_at, nullptr))
            return false;
        --_depth;
        return true;
    }

    // What a script or a command takes: a braced group, or exactly one atom.
    // Pushes the items.
    bool argument_items(const std::string& of, size_t position)
    {
        const long character = peek();
        if (character == '{')
            return group();
        if (character == END || character == '}' || character == '^' || character == '_' ||
            character == '\'')
            return refuse(RefusalKind::SYNTAX, of + " has nothing after it", position);
        if (!enter(_index))
            return false;
        MathItem parsed;
        if (!atom(parsed))
            return false;
        --_depth;
        _stack.push_back(parsed);
        return true;
    }

    // The same as a sublist field.
    bool argument(const std::string& of, size_t position, Field& out)
    {
        const size_t base = _stack.size();
        if (!argument_items(of, position))
            return false;
        out = take(base);
        return true;
    }

    // The stack from `base` as a sublist field; the stack is cut back.
    Field take(size_t base)
    {
        const Field field = _list.sub_list(MathView(_stack.data() + base, _stack.size() - base));
        _stack.resize(base);
        return field;
    }

    // The raw characters of `{...}` after a letters command: plain ASCII only,
    // every blank as one space.
    bool braced_text(const std::string& of, size_t position, std::string& out)
    {
        if (peek() != '{')
            return refuse(RefusalKind::SYNTAX, of + " needs a braced argument", position);
        const size_t start = _index + 1;
        const size_t end = find_close(start);
        if (end >= _source.size())
            return refuse(RefusalKind::SYNTAX, "a { that is never closed", _index);
        out.clear();
        for (size_t offset = start; offset < end; ++offset) {
            const uint32_t character = _source[offset];
            const bool blank = py_isspace(character);
            if (character == '{' || character == '\\' || character == '$' ||
                !(blank || (character >= ' ' && character <= '~'))) {
                return refuse(RefusalKind::CHARACTER,
                              of + " takes plain ASCII only, with no commands or braces inside",
                              offset);
            }
            out += blank ? ' ' : static_cast<char>(character);
        }
        _index = end + 1;
        return true;
    }

    // ---- atoms

    // One atom without its scripts. `_index` is on a non-blank that is not `}`.
    bool atom(MathItem& out)
    {
        const size_t position = _index;
        const uint32_t character = _source[position];
        if (character == '{') {
            const size_t base = _stack.size();
            if (!group())
                return false;
            if (_stack.size() - base == 1) {
                const MathItem& only = _stack[base];
                if (!only.is_fraction && only.kind == AtomType::ORD && only.sup.empty() &&
                    only.sub.empty()) {
                    // tex.web section 1186: a group holding one bare ordinary
                    // atom is that atom.
                    out = only;
                    _stack.resize(base);
                    return true;
                }
            }
            out = MathItem::atom(AtomType::ORD, take(base));
            return true;
        }
        if (character == '\\')
            return command(position, out);
        ++_index;
        if (ascii_alpha(static_cast<long>(character))) {
            out = character_atom(AtomType::ORD, italic(character));
            return true;
        }
        if (ascii_digit(static_cast<long>(character))) {
            out = character_atom(AtomType::ORD, character);
            return true;
        }
        const CharacterEntry* found = find_character(CHARACTERS, character);
        if (found == nullptr)
            found = find_character(UNICODE, character);
        if (found != nullptr) {
            out = character_atom(found->kind, found->codepoint, found->limits);
            return true;
        }
        if (character == '~') {
            out = space_atom(" ");
            return true;
        }
        if (character == '&') {
            return refuse(RefusalKind::ALIGNMENT,
                          "& separates columns, and nothing here is aligned", position);
        }
        if (character == '#') {
            return refuse(RefusalKind::MACRO, "# is a macro parameter, and there are none here",
                          position);
        }
        if (character == '$')
            return refuse(RefusalKind::SYNTAX, "a $ inside a formula", position);
        if (character == '%') {
            return refuse(RefusalKind::SYNTAX, "% starts a comment; write \\% for the sign",
                          position);
        }
        return refuse(RefusalKind::CHARACTER,
                      "the character " + python_char_repr(character) +
                          mathtext_formatted(" (U+%04X) is not in the subset", character),
                      position);
    }

    MathItem space_atom(std::string_view name) const
    {
        const SpaceEntry* space = find_named(SPACES, name);
        MuGlue mu;
        if (space != nullptr) {
            mu.width = space->width;
            mu.stretch = space->stretch;
            mu.shrink = space->shrink;
        }
        return MathItem::space(mu);
    }

    // A run of characters as ordinary atoms, in the math face's upright forms.
    Field upright(std::string_view letters)
    {
        const size_t base = _stack.size();
        for (const char letter : letters) {
            _stack.push_back(character_atom(
                AtomType::ORD, static_cast<uint32_t>(static_cast<unsigned char>(letter))));
        }
        return take(base);
    }

    bool command(size_t position, MathItem& out)
    {
        std::string name;
        if (!command_name(name))
            return false;
        if (_name_is_blank)
            name = " ";
        const std::string shown = name == " " ? std::string("backslash-space") : "\\" + name;

        if (const SymbolEntry* symbol = find_named(SYMBOLS, name)) {
            if (symbol->upright != nullptr) {
                out = MathItem::atom(symbol->kind, upright(symbol->upright), Field(), Field(),
                                     symbol->limits);
            } else {
                out = character_atom(symbol->kind, symbol->codepoint, symbol->limits);
            }
            return true;
        }
        if (name == "frac") {
            Field numerator;
            Field denominator;
            if (!argument(shown, position, numerator) || !argument(shown, position, denominator))
                return false;
            const MathItem fraction = MathItem::fraction(numerator, denominator);
            out = MathItem::atom(AtomType::INNER, _list.sub_list(MathView(&fraction, 1)));
            return true;
        }
        if (name == "overline" || name == "underline") {
            Field nucleus;
            if (!argument(shown, position, nucleus))
                return false;
            out = MathItem::atom(name == "overline" ? AtomType::OVER : AtomType::UNDER, nucleus);
            return true;
        }
        if (find_named(SPACES, name) != nullptr) {
            out = space_atom(name);
            return true;
        }
        if (name == "sqrt") {
            if (peek() == '[') {
                const RefusedEntry* root = find_named(REFUSED, "root");
                return refuse(root->kind, std::string("\\sqrt[n] ") + root->reason, position);
            }
            Field radicand;
            if (!argument(shown, position, radicand))
                return false;
            out = MathItem::atom(AtomType::RADICAL, radicand);
            return true;
        }
        if (name == "binom")
            return binomial(shown, position, out);
        if (name == "left")
            return left_right(position, out);
        if (name == "right")
            return refuse(RefusalKind::SYNTAX, "a \\right with no \\left before it", position);
        if (const ProseEntry* prose_command = find_named(PROSE, name)) {
            std::string words;
            if (!prose(shown, position, words))
                return false;
            out = MathItem::atom(AtomType::ORD, _list.text(words, prose_command->face));
            return true;
        }
        if (find_named(TEXT_COMMANDS, name) != nullptr) {
            std::string letters;
            if (!braced_text(shown, position, letters))
                return false;
            std::string packed;
            for (const char letter : letters) {
                if (letter != ' ')
                    packed += letter;
            }
            const Field nucleus = upright(packed);
            if (name == "mathrm")
                out = MathItem::atom(AtomType::ORD, nucleus);
            else
                out = MathItem::atom(AtomType::OP, nucleus, Field(), Field(), Limits::NO_LIMITS);
            return true;
        }
        if (find_named(ALPHABET_COMMANDS, name) != nullptr)
            return alphabet(name, shown, position, out);
        if (name == "limits" || name == "nolimits")
            return refuse(RefusalKind::SYNTAX, shown + " must follow an operator", position);
        if (name == "begin" && !refuse_environment(position))
            return false;
        if (const RefusedEntry* refused = find_named(REFUSED, name))
            return refuse(refused->kind, shown + " " + refused->reason, position);
        return refuse(RefusalKind::UNKNOWN_COMMAND, shown + " is not in the subset", position);
    }

    // `\left<d> ... \right<d>` with `_index` just past `\left`: one Inner atom
    // whose nucleus is LEFT, body..., RIGHT (tex.web section 1191's
    // math_left_right), so the pair spaces as Inner outside, as Open and Close
    // inside, and scripts hang off the whole of it. The delimiters are sized by
    // the engine when the list is converted.
    bool left_right(size_t position, MathItem& out)
    {
        if (!enter(position))
            return false;
        Field opening;
        if (!delimiter("\\left", position, opening))
            return false;
        const size_t base = _stack.size();
        _stack.push_back(MathItem::atom(AtomType::LEFT, opening));
        if (!items(nullptr, &position))
            return false;
        const size_t closed_at = _index;
        std::string right;
        if (!command_name(right))
            return false;
        Field closing;
        if (!delimiter("\\right", closed_at, closing))
            return false;
        --_depth;
        _stack.push_back(MathItem::atom(AtomType::RIGHT, closing));
        out = MathItem::atom(AtomType::INNER, take(base));
        return true;
    }

    // `\binom{n}{k}`: one over the other with no bar, between parentheses that
    // fit — {\left( {n \atop k} \right)}, an Ord. Not quite TeX's:
    // \atopwithdelims takes its delimiters at a size the font states for the
    // style (tex.web section 748) where make_left_right fits them to the
    // content.
    bool binomial(const std::string& shown, size_t position, MathItem& out)
    {
        if (!enter(position))
            return false;
        Field upper;
        Field lower;
        if (!argument(shown, position, upper) || !argument(shown, position, lower))
            return false;
        --_depth;
        const MathItem parts[3] = {
            MathItem::atom(AtomType::LEFT, Field::math_char(0x0028)),
            MathItem::fraction(upper, lower, 0),
            MathItem::atom(AtomType::RIGHT, Field::math_char(0x0029)),
        };
        out = MathItem::atom(AtomType::ORD, _list.sub_list(MathView(parts, 3)));
        return true;
    }

    // The delimiter after a \left or \right: a math character, or an empty
    // field for `.`.
    bool delimiter(const char* of, size_t position, Field& out)
    {
        const long character = peek();
        const size_t here = _index;
        if (character == END) {
            return refuse(RefusalKind::SYNTAX, std::string(of) + " has no delimiter after it",
                          position);
        }
        if (character == '\\') {
            std::string name;
            if (!command_name(name))
                return false;
            const DelimiterCommandEntry* found = find_named(DELIMITER_COMMANDS, name);
            if (found == nullptr) {
                return refuse(RefusalKind::DELIMITER,
                              "\\" + name + " is not a delimiter " + of + " can size", here);
            }
            out = Field::math_char(found->codepoint);
            return true;
        }
        const DelimiterCharacterEntry* found = nullptr;
        for (const DelimiterCharacterEntry& entry : DELIMITER_CHARACTERS) {
            if (static_cast<long>(entry.character) == character)
                found = &entry;
        }
        if (found == nullptr) {
            return refuse(RefusalKind::DELIMITER,
                          python_char_repr(static_cast<uint32_t>(character)) +
                              " is not a delimiter " + of + " can size",
                          here);
        }
        ++_index;
        out = found->null ? Field() : Field::math_char(found->codepoint);
        return true;
    }

    // The words of `\text{...}`: anything printable, with no markup inside.
    // Blanks are kept, a run of them as one space, because `\text{ if }` is how
    // a formula asks for the air around a word. A brace, a backslash or a
    // dollar inside would be a group, a command or math within the text; none
    // of that is done, so each is refused and not set as itself.
    bool prose(const std::string& of, size_t position, std::string& out)
    {
        if (peek() != '{')
            return refuse(RefusalKind::SYNTAX, of + " needs a braced argument", position);
        const size_t start = _index + 1;
        const size_t end = find_close(start);
        if (end >= _source.size())
            return refuse(RefusalKind::SYNTAX, "a { that is never closed", _index);
        out.clear();
        for (size_t offset = start; offset < end; ++offset) {
            const uint32_t character = _source[offset];
            if (py_isspace(character)) {
                if (out.empty() || out.back() != ' ')
                    out += ' ';
            } else if (character == '{' || character == '\\' || character == '$') {
                return refuse(
                    RefusalKind::TEXT,
                    of + " takes plain words only, with no commands, braces or math inside",
                    offset);
            } else if (!py_isprintable(character)) {
                return refuse(RefusalKind::CHARACTER,
                              mathtext_formatted("the character U+%04X in ", character) + of +
                                  " cannot be printed",
                              offset);
            } else {
                mathtext_append_utf8(out, character);
            }
        }
        _index = end + 1;
        return true;
    }

    bool alphabet(const std::string& name, const std::string& shown, size_t position, MathItem& out)
    {
        const size_t start = _index;
        std::string letters;
        if (!braced_text(shown, position, letters))
            return false;
        const size_t base = _stack.size();
        for (const char letter : letters) {
            if (letter == ' ')
                continue;
            const uint32_t character = static_cast<uint32_t>(static_cast<unsigned char>(letter));
            uint32_t codepoint = 0;
            if (!alphabet_form(name, character, &codepoint)) {
                _stack.resize(base);
                return refuse(RefusalKind::CHARACTER,
                              shown + " has no form of " + python_char_repr(character), start);
            }
            _stack.push_back(character_atom(AtomType::ORD, codepoint));
        }
        if (_stack.size() - base == 1) {
            out = _stack[base];
            _stack.resize(base);
            return true;
        }
        out = MathItem::atom(AtomType::ORD, take(base));
        return true;
    }

    // `\begin{...}`: a matrix if it names one. True means it did not refuse, and
    // the caller goes on to refuse \begin as an environment.
    bool refuse_environment(size_t position)
    {
        size_t at = _index;
        while (at < _source.size() && py_isspace(_source[at]))
            ++at;
        if (at >= _source.size() || _source[at] != '{')
            return true;
        const size_t close = find_close(at + 1);
        // With no closing brace the reference takes the name to be empty.
        const std::string name = close < _source.size() ? text(at + 1, close) : std::string();
        std::string_view stem(name);
        while (!stem.empty() && stem.back() == '*')
            stem.remove_suffix(1);
        if (find_named(MATRIX_ENVIRONMENTS, stem) != nullptr || name == "cases" ||
            name == "substack") {
            const RefusedEntry* matrix = find_named(REFUSED, "matrix");
            return refuse(matrix->kind, "\\begin{" + name + "} " + matrix->reason, position);
        }
        return true;
    }

    // ---- scripts

    // `base` with whatever `^`, `_`, `'`, \limits and \nolimits follow it.
    bool scripts(const MathItem& base, MathItem& out)
    {
        std::vector<MathItem> sup;
        std::vector<MathItem> sub;
        bool has_sup = false;
        bool has_sub = false;
        bool primed = false;
        Limits limits = base.limits;
        for (;;) {
            const long character = peek();
            const size_t here = _index;
            if (character == '\'') {
                if (has_sup && !primed)
                    return refuse(RefusalKind::SYNTAX, "a prime after a superscript", here);
                ++_index;
                has_sup = true;
                sup.push_back(character_atom(AtomType::ORD, 0x2032));
                primed = true;
            } else if (character == '^') {
                if (has_sup && !primed)
                    return refuse(RefusalKind::SYNTAX, "a second superscript on one atom", here);
                ++_index;
                has_sup = true;
                const size_t base_of_stack = _stack.size();
                if (!argument_items("^", here))
                    return false;
                sup.insert(sup.end(), _stack.begin() + static_cast<std::ptrdiff_t>(base_of_stack),
                           _stack.end());
                _stack.resize(base_of_stack);
                primed = false;
            } else if (character == '_') {
                if (has_sub)
                    return refuse(RefusalKind::SYNTAX, "a second subscript on one atom", here);
                ++_index;
                has_sub = true;
                const size_t base_of_stack = _stack.size();
                if (!argument_items("_", here))
                    return false;
                sub.assign(_stack.begin() + static_cast<std::ptrdiff_t>(base_of_stack),
                           _stack.end());
                _stack.resize(base_of_stack);
            } else if (character == '\\' &&
                       (starts_with("\\limits", here) || starts_with("\\nolimits", here))) {
                std::string name;
                if (!command_name(name))
                    return false;
                if (name != "limits" && name != "nolimits") {
                    _index = here;
                    break;
                }
                if (base.is_fraction || base.kind != AtomType::OP) {
                    return refuse(RefusalKind::SYNTAX, "\\" + name + " must follow an operator",
                                  here);
                }
                limits = name == "limits" ? Limits::LIMITS : Limits::NO_LIMITS;
            } else {
                break;
            }
        }
        out = base;
        if (has_sup)
            out.sup = _list.sub_list(sup);
        if (has_sub)
            out.sub = _list.sub_list(sub);
        out.limits = limits;
        return true;
    }

    const std::vector<uint32_t>& _source;
    MathList& _list;
    std::vector<MathItem> _stack;
    size_t _index = 0;
    int _depth = 0;
    // Whether the name command_name last read is one white-space character,
    // which is the command backslash-space whatever the character.
    bool _name_is_blank = false;
    RefusalKind _kind = RefusalKind::EMPTY;
    std::string _reason;
    size_t _position = 0;
};

// Python's f"{needed / largest:.2f}": the quotient as a double, then the exact
// value of that double to two places, ties to even. Written with integers
// because printf takes its decimal separator from LC_NUMERIC, which belongs to
// the host. `largest` is not zero.
std::string quotient_to_two_places(Scaled needed, Scaled largest)
{
    // The reference divides two integers, which is the correctly rounded
    // quotient, and so is this while both fit 53 bits.
    const double value = static_cast<double>(needed) / static_cast<double>(largest);
    const double magnitude = std::fabs(value);
    const double whole_part = std::floor(magnitude);
    // Both are at most 2^63, the magnitude of the least Scaled over one.
    uint64_t whole = static_cast<uint64_t>(whole_part);
    const double fraction = magnitude - whole_part; // exact
    uint64_t hundredths = 0;
    // Below 2^-10 a hundred times the fraction is under a tenth. At or above
    // it the fraction's last bit is no lower than 2^-63, so it is an integer
    // of 63 bits over 2^63; the product by 100 is taken in two halves.
    if (fraction >= 1.0 / 1024.0) {
        const uint64_t scaled = static_cast<uint64_t>(std::ldexp(fraction, 63));
        const uint64_t low = (scaled & 0xFFFFFFFFu) * 100u;
        const uint64_t high = (scaled >> 32) * 100u + (low >> 32);
        hundredths = high >> 31;
        const uint64_t rest = ((high & 0x7FFFFFFFu) << 32) | (low & 0xFFFFFFFFu);
        const uint64_t half = uint64_t(1) << 62;
        if (rest > half || (rest == half && (hundredths & 1u) != 0))
            ++hundredths;
        if (hundredths == 100) {
            hundredths = 0;
            ++whole;
        }
    }
    return mathtext_formatted("%s%llu.%02u", std::signbit(value) ? "-" : "",
                              static_cast<unsigned long long>(whole),
                              static_cast<unsigned>(hundredths));
}

// The byte offset of codepoint `position` of a source decoded into `offsets`.
size_t byte_offset(const std::vector<uint32_t>& offsets, size_t position, size_t byte_count)
{
    return position < offsets.size() ? offsets[position] : byte_count;
}

// Python's " ".join(text.split()): runs of white space become one space and
// the ends are trimmed.
std::string joined_words(const std::string& text)
{
    std::vector<uint32_t> codepoints;
    decode_utf8(text, codepoints);
    std::string out;
    bool pending = false;
    for (const uint32_t codepoint : codepoints) {
        if (py_isspace(codepoint)) {
            pending = !out.empty();
            continue;
        }
        if (pending)
            out += ' ';
        pending = false;
        mathtext_append_utf8(out, codepoint);
    }
    return out;
}

} // namespace

const char* refusal_kind_value(RefusalKind kind)
{
    static const char* const values[REFUSAL_KIND_COUNT] = {
        "empty",   "syntax",    "nesting", "unknown-command", "character", "macro",  "environment",
        "matrix",  "alignment", "infix",   "delimiter",       "radical",   "accent", "style",
        "spacing", "text",      "font",    "layout",
    };
    const int index = static_cast<int>(kind);
    return index >= 0 && index < REFUSAL_KIND_COUNT ? values[index] : "";
}

bool parse(std::string_view source, bool display, Formula& formula, Refusal& refusal)
{
    formula.list.clear();
    formula.items = MathSpan();
    formula.display = display;
    refusal = Refusal();

    if (source.size() > MAX_SOURCE_BYTES) {
        refusal.kind = RefusalKind::LAYOUT;
        refusal.reason =
            mathtext_formatted("the formula is too large to set: it is longer than %u bytes, "
                               "which is the longest that is read",
                               static_cast<unsigned>(MAX_SOURCE_BYTES));
        return false;
    }

    std::vector<uint32_t> codepoints;
    std::vector<uint32_t> offsets;
    decode_utf8(source, codepoints, &offsets);

    Parser parser(codepoints, formula.list);
    if (parser.formula(formula.items))
        return true;

    formula.list.clear();
    formula.items = MathSpan();
    refusal.kind = parser.kind();
    refusal.reason = parser.reason();
    refusal.position =
        parser.position() < codepoints.size() ? parser.position() : codepoints.size();
    refusal.byte_position = byte_offset(offsets, refusal.position, source.size());
    return false;
}

bool set_formula(const Formula& formula, MathFonts& fonts, NodeArena& arena, Node& box,
                 MathStatus& status)
{
    const NodeArena::Mark mark = arena.mark();
    std::vector<Node> hlist;
    status = mlist_to_hlist(formula.list, formula.items, formula.style(), fonts, arena, hlist);
    if (!status.ok())
        return false;
    const PackResult packed = hpack(arena, hlist, 0, PackMode::ADDITIONAL, fonts.measure());
    const Scaled outside = dimension_outside_max_dimen(packed.box);
    if (packed.error != PackError::NONE) {
        status.error = MathError::MALFORMED;
    } else if (fonts.unmeasured()) {
        status.error = MathError::TEXT_FACE_UNAVAILABLE;
    } else if (outside != 0) {
        status.error = MathError::TOO_LARGE;
        status.needed = outside;
        status.largest = MAX_DIMEN;
    }
    if (!status.ok()) {
        arena.rewind(mark);
        return false;
    }
    box = arena.add_box(packed.box, hlist);
    return true;
}

Refusal refusal_from(const MathStatus& status, std::string_view source, const Formula& formula,
                     const MathFonts& fonts)
{
    Refusal refusal;
    refusal.kind = RefusalKind::FONT;
    switch (status.error) {
    case MathError::MISSING_GLYPH: {
        refusal.reason =
            std::string("the ") + face_value(status.face) + " face has no glyph for " +
            python_char_repr(status.codepoint) +
            mathtext_formatted(" (U+%04X), so its words cannot be set", status.codepoint);
        std::vector<uint32_t> codepoints;
        std::vector<uint32_t> offsets;
        decode_utf8(source, codepoints, &offsets);
        for (size_t i = 0; i < codepoints.size(); ++i) {
            if (codepoints[i] == status.codepoint) {
                refusal.position = i;
                refusal.byte_position = offsets[i];
                break;
            }
        }
        return refusal;
    }
    case MathError::TEXT_FACE_UNAVAILABLE: {
        std::string what;
        std::string_view words;
        if (fonts.text()) {
            what = std::string(face_value(status.face)) + " did not resolve";
        } else if (formula.list.text_of(status.text, &words)) {
            std::vector<uint32_t> codepoints;
            decode_utf8(words, codepoints);
            what = python_repr(codepoints.data(), codepoints.size()) +
                   " is prose, and these MathFonts were built without `text`";
        }
        refusal.reason = joined_words("there is no prose face to set its words in: " + what);
        return refusal;
    }
    case MathError::DELIMITER_TOO_SMALL: {
        refusal.kind = RefusalKind::LAYOUT;
        if (status.largest > 0) {
            std::string what = "what is between the delimiters";
            std::string which;
            if (status.codepoint == SURD) {
                what = "what is under the root";
                which = "radical sign";
            } else {
                mathtext_append_utf8(which, status.codepoint);
            }
            refusal.reason = what + " needs a " + which + " " +
                             quotient_to_two_places(status.needed, status.largest) +
                             " times as tall as the largest the math face has, and one is not "
                             "assembled from pieces here";
        } else {
            refusal.reason =
                joined_words("the math engine will not set it: " + math_error_message(status));
        }
        return refusal;
    }
    case MathError::TOO_LARGE:
    case MathError::TOO_DEEP:
        refusal.kind = RefusalKind::LAYOUT;
        refusal.reason = "the formula is too large to set: " + math_error_message(status);
        return refusal;
    default:
        break;
    }
    refusal.reason = joined_words("the math face could not set it: " + math_error_message(status));
    return refusal;
}

std::string c_string_reason(std::string_view reason)
{
    std::string out;
    out.reserve(reason.size());
    for (const char byte : reason) {
        if (byte == '\0')
            out += "\\x00";
        else
            out += byte;
    }
    return out;
}

bool typeset(std::string_view source, bool display, MathFonts& fonts, NodeArena& arena,
             Formula& formula, Node& box, Refusal& refusal)
{
    if (!parse(source, display, formula, refusal))
        return false;
    MathStatus status;
    if (set_formula(formula, fonts, arena, box, status))
        return true;
    refusal = refusal_from(status, source, formula, fonts);
    return false;
}

size_t supported_command_count()
{
    return IM_ARRAYSIZE(SUPPORTED);
}

const char* supported_command(size_t index)
{
    return index < IM_ARRAYSIZE(SUPPORTED) ? SUPPORTED[index] : "";
}

size_t refused_command_count()
{
    return IM_ARRAYSIZE(REFUSED);
}

RefusedCommand refused_command(size_t index)
{
    RefusedCommand out;
    if (index < IM_ARRAYSIZE(REFUSED)) {
        out.name = REFUSED[index].name;
        out.kind = REFUSED[index].kind;
        out.reason_tail = REFUSED[index].reason;
    }
    return out;
}

size_t sized_delimiter_count()
{
    return IM_ARRAYSIZE(SIZED_DELIMITERS);
}

SizedDelimiter sized_delimiter(size_t index)
{
    SizedDelimiter out;
    if (index < IM_ARRAYSIZE(SIZED_DELIMITERS)) {
        out.written = SIZED_DELIMITERS[index].written;
        out.codepoint = SIZED_DELIMITERS[index].codepoint;
    }
    return out;
}

size_t prose_command_count()
{
    return IM_ARRAYSIZE(PROSE);
}

ProseCommand prose_command(size_t index)
{
    ProseCommand out;
    if (index < IM_ARRAYSIZE(PROSE)) {
        out.name = PROSE[index].name;
        out.face = PROSE[index].face;
    }
    return out;
}

} // namespace ImTypeset::detail
