// Reader for the golden files under tests/golden, which the oracle scripts under
// tools/oracle write from the Python reference. The C++ tests replay each
// file's inputs through the port and compare with what the reference answered;
// there is no Python on this side.
//
// The base format, shared by every oracle script:
//
//   - UTF-8 text, one record per line, '\n' line ends (a trailing '\r' is dropped).
//   - A line whose first non-blank character is '#' is a comment. Blank lines
//     are skipped.
//   - A record is tokens separated by spaces. The first token is the record's
//     tag; the rest are its fields.
//   - Integer field: decimal, optional leading '-'.
//   - Float field: what Python's float.hex() prints -- '0x1.8000000000000p+1',
//     '-0x0.0p+0', 'inf', '-inf', 'nan' -- read with strtod, so the double is
//     reproduced bit for bit.
//   - String field: UTF-8 with every byte that is not printable ASCII, plus
//     space and '%', written as %XX (two hex digits, either case). The empty
//     string is the single character '%'. A token therefore never contains a
//     space and is never empty.
//
// Which tags exist and what their fields mean belongs to each oracle script and
// is documented beside it (tools/oracle/FORMAT*.md).
//
// A malformed field, a field index past the end of a record and an unreadable
// file are all reported through check.h as failures naming the golden file and
// line, and the getter returns zero or empty, so a broken golden fails the test
// that reads it and does not crash it.

#pragma once

#include "check.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace golden
{

// tests/golden/<relative>
inline std::string path(std::string_view relative)
{
    std::string out = IMTYPESET_GOLDEN_DIR "/";
    out += relative;
    return out;
}

// tests/data/<relative>, e.g. data_path("fonts/Roboto-Regular.ttf").
inline std::string data_path(std::string_view relative)
{
    std::string out = IMTYPESET_TEST_DATA_DIR "/";
    out += relative;
    return out;
}

// Reads a whole file as bytes. False, with `out` empty, if it cannot be read.
template<typename Container>
bool read_file(const std::string& file_path, Container& out)
{
    out.clear();
    std::FILE* f = std::fopen(file_path.c_str(), "rb");
    if (f == nullptr)
        return false;
    bool ok = true;
    char buffer[65536];
    for (;;) {
        const size_t n = std::fread(buffer, 1, sizeof buffer, f);
        out.insert(out.end(), buffer, buffer + n);
        if (n < sizeof buffer) {
            ok = std::ferror(f) == 0;
            break;
        }
    }
    std::fclose(f);
    if (!ok)
        out.clear();
    return ok;
}

// Full paths of the regular files directly inside tests/golden/<subdir> whose
// names end in `suffix`, sorted by name so a run visits them in a stable order.
// Empty if the directory is missing: a test that expects goldens should
// REQUIRE(!files.empty()), or a missing directory reads as a pass.
inline std::vector<std::string> list(std::string_view subdir, std::string_view suffix = ".golden")
{
    std::vector<std::string> out;
    std::error_code ec;
    std::filesystem::directory_iterator it(path(subdir), ec);
    const std::filesystem::directory_iterator end;
    while (!ec && it != end) {
        std::error_code entry_ec;
        if (it->is_regular_file(entry_ec) && !entry_ec) {
            std::string name = it->path().string();
            if (name.size() >= suffix.size() &&
                std::string_view(name).substr(name.size() - suffix.size()) == suffix)
                out.push_back(std::move(name));
        }
        it.increment(ec);
    }
    std::sort(out.begin(), out.end());
    return out;
}

inline int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Decodes a string token. False if a '%' is not followed by two hex digits; the
// lone '%' that stands for the empty string is not such a case.
inline bool percent_decode(std::string_view token, std::string& out)
{
    out.clear();
    if (token == "%")
        return true;
    out.reserve(token.size());
    for (size_t i = 0; i < token.size(); ++i) {
        if (token[i] != '%') {
            out += token[i];
            continue;
        }
        if (i + 2 >= token.size())
            return false;
        const int hi = hex_digit(token[i + 1]);
        const int lo = hex_digit(token[i + 2]);
        if (hi < 0 || lo < 0)
            return false;
        out += static_cast<char>(hi * 16 + lo);
        i += 2;
    }
    return true;
}

// The inverse, for a test that wants to print or build a token. Encodes exactly
// what the format says must be encoded.
inline std::string percent_encode(std::string_view text)
{
    if (text.empty())
        return "%";
    std::string out;
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c <= 0x20 || c >= 0x7f || c == '%')
            out += check::format("%%%02X", static_cast<unsigned>(c));
        else
            out += ch;
    }
    return out;
}

// One line of a golden file. Views point into the File that produced it, so a
// Record must not outlive its File.
struct Record {
    std::string_view tag;
    std::vector<std::string_view> fields; // the tokens after the tag
    const char* file = "";                // the File's name, for messages
    int line = 0;                         // 1-based

    size_t size() const { return fields.size(); }
    bool is(std::string_view t) const { return tag == t; }

    void error(size_t index, const char* what) const
    {
        const std::string token = index < fields.size() ? std::string(fields[index]) : "<missing>";
        check::fail(file, line,
                    check::format("golden record '%.*s' field %zu: %s (token '%s')",
                                  static_cast<int>(tag.size()), tag.data(), index, what,
                                  token.c_str()));
    }

    // The token as written, undecoded. Empty, and a failure, if out of range.
    std::string_view raw(size_t index) const
    {
        if (index >= fields.size()) {
            error(index, "record has no such field");
            return std::string_view();
        }
        return fields[index];
    }

    int64_t i64(size_t index) const
    {
        const std::string token(raw(index));
        if (token.empty())
            return 0;
        errno = 0;
        char* end = nullptr;
        const long long v = std::strtoll(token.c_str(), &end, 10);
        if (end != token.c_str() + token.size()) {
            error(index, "not a decimal integer");
            return 0;
        }
        if (errno == ERANGE) {
            error(index, "integer does not fit in 64 bits");
            return 0;
        }
        return static_cast<int64_t>(v);
    }

    // As i64, and a failure if the value does not fit an int.
    int i32(size_t index) const
    {
        const int64_t v = i64(index);
        if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) {
            error(index, "integer does not fit in an int");
            return 0;
        }
        return static_cast<int>(v);
    }

    // As i64, and a failure if the value is negative or does not fit 32 bits:
    // codepoints, counts, indices.
    uint32_t u32(size_t index) const
    {
        const int64_t v = i64(index);
        if (v < 0 || v > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
            error(index, "integer does not fit in 32 unsigned bits");
            return 0;
        }
        return static_cast<uint32_t>(v);
    }

    // An integer field holding 0 or 1.
    bool boolean(size_t index) const
    {
        const int64_t v = i64(index);
        if (v != 0 && v != 1) {
            error(index, "not 0 or 1");
            return false;
        }
        return v == 1;
    }

    double f64(size_t index) const
    {
        const std::string token(raw(index));
        if (token.empty())
            return 0.0;
        char* end = nullptr;
        const double v = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size()) {
            error(index, "not a float");
            return 0.0;
        }
        return v;
    }

    // The percent-decoded string.
    std::string str(size_t index) const
    {
        std::string out;
        const std::string_view token = raw(index);
        if (token.empty())
            return out;
        if (!percent_decode(token, out)) {
            error(index, "bad percent-encoding");
            out.clear();
        }
        return out;
    }
};

// Reads a record's fields left to right, for records whose layout is a sequence
// with counts in it ("n then n pairs").
class Cursor
{
public:
    explicit Cursor(const Record& record, size_t start = 0)
        : _record(&record)
        , _next(start)
    {
    }

    size_t remaining() const { return _next < _record->size() ? _record->size() - _next : 0; }
    bool done() const { return remaining() == 0; }
    size_t position() const { return _next; }

    std::string_view raw() { return _record->raw(_next++); }
    int64_t i64() { return _record->i64(_next++); }
    int i32() { return _record->i32(_next++); }
    uint32_t u32() { return _record->u32(_next++); }
    bool boolean() { return _record->boolean(_next++); }
    double f64() { return _record->f64(_next++); }
    std::string str() { return _record->str(_next++); }

    // A failure if fields are left over: a record longer than its reader expects
    // means the format moved and the test did not.
    bool expect_done() const
    {
        if (done())
            return true;
        _record->error(_next, "unread trailing field");
        return false;
    }

private:
    const Record* _record;
    size_t _next;
};

// A parsed golden file. Owns the text the records view.
class File
{
public:
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    // Loads and parses. On failure reports through check.h and returns false,
    // leaving no records.
    bool load(const std::string& file_path)
    {
        _name = file_path;
        _records.clear();
        if (!read_file(file_path, _text)) {
            check::fail(_name.c_str(), 0, "cannot read golden file");
            return false;
        }
        split();
        return true;
    }

    // Parses text held in memory; `name` stands in for the path in messages.
    void parse(std::string text, std::string name)
    {
        _name = std::move(name);
        _text = std::move(text);
        split();
    }

    const std::string& name() const { return _name; }
    const std::vector<Record>& records() const { return _records; }
    std::vector<Record>::const_iterator begin() const { return _records.begin(); }
    std::vector<Record>::const_iterator end() const { return _records.end(); }

private:
    static bool blank(char c) { return c == ' ' || c == '\t'; }

    void split()
    {
        _records.clear();
        const std::string_view text(_text);
        size_t pos = 0;
        int line_number = 0;
        while (pos < text.size()) {
            size_t eol = text.find('\n', pos);
            if (eol == std::string_view::npos)
                eol = text.size();
            std::string_view line = text.substr(pos, eol - pos);
            pos = eol + 1;
            ++line_number;
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);

            Record record;
            record.file = _name.c_str();
            record.line = line_number;
            size_t i = 0;
            bool first = true;
            while (i < line.size()) {
                while (i < line.size() && blank(line[i]))
                    ++i;
                if (i == line.size() || (first && line[i] == '#'))
                    break;
                const size_t token_start = i;
                while (i < line.size() && !blank(line[i]))
                    ++i;
                const std::string_view token = line.substr(token_start, i - token_start);
                if (first)
                    record.tag = token;
                else
                    record.fields.push_back(token);
                first = false;
            }
            if (!first)
                _records.push_back(std::move(record));
        }
    }

    std::string _name;
    std::string _text;
    std::vector<Record> _records;
};

} // namespace golden
