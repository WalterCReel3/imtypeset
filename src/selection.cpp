#include "selection.h"

#include "unicode_tables.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace ImTypeset::detail
{

namespace
{

// CPython's bisect_left and bisect_right, step for step, so that an array a
// caller built out of order is answered as the reference answers it.
uint32_t bisect_left(const Boundary* values, uint32_t count, int64_t wanted)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2;
        if (static_cast<int64_t>(values[middle]) < wanted)
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

uint32_t bisect_right(const Boundary* values, uint32_t count, int64_t wanted)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2;
        if (wanted < static_cast<int64_t>(values[middle]))
            high = middle;
        else
            low = middle + 1;
    }
    return low;
}

// A query point brought inside the range the arithmetic below is safe in: a
// point is whatever a host passes.
Scaled clamp_coordinate(Scaled value)
{
    return value > SCALED_LIMIT ? SCALED_LIMIT : (value < -SCALED_LIMIT ? -SCALED_LIMIT : value);
}

// A coordinate or an extent out of a command buffer, as the model keeps it.
// What ship-out writes is sums of node dimensions and far inside this
// (src/units.h); a buffer made by hand holds anything. Held to a quarter of
// SCALED_LIMIT, the sum of two of them (y + depth, x + width) is inside half
// of it, and the difference between such a sum and a clamped point is inside
// an int64, which is all the arithmetic here does with them.
constexpr Scaled HELD_LIMIT = SCALED_LIMIT / 4;

Scaled held(Scaled value)
{
    return value > HELD_LIMIT ? HELD_LIMIT : (value < -HELD_LIMIT ? -HELD_LIMIT : value);
}

// How far x is outside [left, left + width], 0 inside or on either end.
Scaled beside(const Fragment& f, Scaled x)
{
    const Scaled before = f.x - x;
    const Scaled after = x - (f.x + f.width);
    const Scaled outside = before > after ? before : after;
    return outside > 0 ? outside : 0;
}

// What a run at word granularity is made of.
enum class Class : uint8_t { WORD, SPACE, OTHER, ATOM };

Class classify(ItemKind kind, uint32_t codepoint)
{
    if (kind == ItemKind::ATOM)
        return Class::ATOM; // a formula is a run of its own
    if (kind != ItemKind::GLYPH)
        return Class::SPACE;
    if (is_alnum(codepoint) || codepoint == '_')
        return Class::WORD;
    if (is_space(codepoint))
        return Class::SPACE;
    return Class::OTHER;
}

void selection_append_utf8(std::string& out, uint32_t codepoint)
{
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
        codepoint = 0xFFFD;
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
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

// One of TextData's arrays, searched by index: by bisection when it is in the
// order its contract asks for, which is checked once and not assumed, and
// entry by entry when it is not. The first entry with an index is the one
// found either way.
class ByIndex
{
public:
    ByIndex(const IndexedText* entries, size_t count)
        : _entries(entries)
        , _count(entries != nullptr ? count : 0)
    {
        for (size_t i = 1; i < _count && _sorted; ++i)
            _sorted = _entries[i - 1].index <= _entries[i].index;
    }

    const IndexedText* find(uint32_t index) const
    {
        const IndexedText* const end = _entries + _count;
        if (!_sorted) {
            for (const IndexedText* entry = _entries; entry != end; ++entry) {
                if (entry->index == index)
                    return entry;
            }
            return nullptr;
        }
        const IndexedText* found =
            std::lower_bound(_entries, end, index, [](const IndexedText& e, uint32_t wanted) {
                return e.index < wanted;
            });
        return found != end && found->index == index ? found : nullptr;
    }

private:
    const IndexedText* _entries;
    size_t _count;
    bool _sorted = true;
};

// The same for the hard breaks, which are bare indices.
class Breaks
{
public:
    Breaks(const uint32_t* indices, size_t count)
        : _indices(indices)
        , _count(indices != nullptr ? count : 0)
        , _sorted(std::is_sorted(_indices, _indices + _count))
    {
    }

    bool has(uint32_t index) const
    {
        const uint32_t* const end = _indices + _count;
        return _sorted ? std::binary_search(_indices, end, index)
                       : std::find(_indices, end, index) != end;
    }

private:
    const uint32_t* _indices;
    size_t _count;
    bool _sorted;
};

// The reference builds its text as a list of pieces and edits the tail of the
// list as separators arrive. This is that list: one buffer and where each piece
// starts in it.
class Pieces
{
public:
    bool empty() const { return _starts.empty(); }
    size_t count() const { return _starts.size(); }

    void append(const char* text, size_t size)
    {
        _starts.push_back(_buffer.size());
        _buffer.append(text, size);
    }

    // Puts a separator after the text so far. A single space is a wrap and adds
    // nothing after white space. Anything else takes the place of whatever
    // spaces were left dangling before it, and is dropped if nothing but
    // spaces came before.
    void separate(const char* separator, size_t size)
    {
        if (size == 1 && separator[0] == ' ') {
            const char last = _buffer.size() > _starts.back() ? _buffer.back() : '\0';
            if (last != ' ' && last != '\n' && last != '\t')
                append(" ", 1);
            return;
        }
        while (!_starts.empty() && last_is_blank()) {
            _buffer.resize(_starts.back());
            _starts.pop_back();
        }
        if (_starts.empty())
            return;
        while (_buffer.back() == ' ')
            _buffer.pop_back();
        append(separator, size);
    }

    // The first `kept` pieces joined, appended to `out`.
    void join(size_t kept, std::string& out) const
    {
        const size_t size = kept < _starts.size() ? _starts[kept] : _buffer.size();
        out.append(_buffer, 0, size);
    }

private:
    // Whether the last piece is empty or all spaces.
    bool last_is_blank() const
    {
        for (size_t i = _starts.back(); i < _buffer.size(); ++i)
            if (_buffer[i] != ' ')
                return false;
        return true;
    }

    std::string _buffer;
    std::vector<size_t> _starts;
};

} // namespace

// Python's str.isalnum and str.isspace, from the one copy of the tables.
bool is_alnum(uint32_t codepoint)
{
    if (codepoint < 0x80)
        return (codepoint >= '0' && codepoint <= '9') || (codepoint >= 'A' && codepoint <= 'Z') ||
               (codepoint >= 'a' && codepoint <= 'z');
    return py_isalnum(codepoint);
}

bool is_space(uint32_t codepoint)
{
    return py_isspace(codepoint);
}

// --- Building ---------------------------------------------------------------------

void SelectionModel::build(const DrawCmd* cmds, size_t count)
{
    _fragments.clear();
    _indices.clear();
    _kinds.clear();
    _codepoints.clear();
    _edges.clear();
    _clusters.clear();
    _reach_end.clear();
    _lines.clear();
    _stack.clear();
    _owner.clear();
    _item_cmd.clear();
    _length = 0;

    // One forward pass with a stack of the lines that are open: ship-out emits
    // each LINE before what it covers and nests them where boxes nested, so the
    // top of the stack is the innermost line an item is in.
    //
    // The index of the atom whose ink is being passed over, if any: the glyphs
    // of a formula repeat their atom's index and are not items themselves.
    int64_t atom = -1;
    for (size_t position = 0; position < count; ++position) {
        const DrawCmd& cmd = cmds[position];
        switch (cmd.type) {
        case CmdType::LINE: {
            while (!_stack.empty() && cmd.start() >= cmds[_lines[_stack.back()].cmd].end)
                _stack.pop_back();
            LineInfo info;
            info.cmd = static_cast<uint32_t>(position);
            if (!_stack.empty()) {
                _lines[_stack.back()].has_child = true;
                info.outer = static_cast<int32_t>(_stack.front());
            }
            _stack.push_back(static_cast<uint32_t>(_lines.size()));
            _lines.push_back(info);
            break;
        }
        case CmdType::GLYPH:
        case CmdType::SET_GLUE:
        case CmdType::ATOM: {
            if (cmd.type == CmdType::GLYPH && static_cast<int64_t>(cmd.index) == atom)
                break;
            if (cmd.type == CmdType::ATOM)
                atom = static_cast<int64_t>(cmd.index);
            while (!_stack.empty() && cmd.index >= cmds[_lines[_stack.back()].cmd].end)
                _stack.pop_back();
            if (!_stack.empty()) {
                _owner.push_back(_stack.back());
                _item_cmd.push_back(static_cast<uint32_t>(position));
                ++_lines[_stack.back()].items;
            }
            break;
        }
        case CmdType::RULE_RECT:
            // A rule cannot be pointed at; it marks the line it sits in as not
            // being a blank line of text.
            if (!_stack.empty())
                _lines[_stack.back()].ruled = true;
            break;
        }
    }

    // Each line's items, contiguous and in reading order.
    uint32_t total = 0;
    for (LineInfo& line : _lines) {
        line.first_item = total;
        total += line.items;
        line.items = 0; // counted up again as the items are placed
    }
    _indices.resize(total);
    _kinds.resize(total);
    _codepoints.resize(total);
    _stack.resize(total); // reused: the command each placed item came from
    for (size_t i = 0; i < _owner.size(); ++i) {
        LineInfo& line = _lines[_owner[i]];
        const uint32_t slot = line.first_item + line.items++;
        const DrawCmd& cmd = cmds[_item_cmd[i]];
        _indices[slot] = cmd.index;
        _codepoints[slot] = cmd.codepoint;
        if (cmd.type == CmdType::GLYPH)
            _kinds[slot] = ItemKind::GLYPH;
        else if (cmd.type == CmdType::ATOM)
            _kinds[slot] = ItemKind::ATOM;
        else
            _kinds[slot] = cmd.natural() == 0 ? ItemKind::FILL : ItemKind::SPACE;
        _stack[slot] = _item_cmd[i];
    }

    // A line that turned out to hold only other lines is a container, and one
    // that holds a rule and no item is not a line of text; neither is a
    // fragment. A line with no item, no line and no rule inside it is a blank
    // line of text and is.
    for (const LineInfo& info : _lines) {
        if (info.items == 0 && (info.has_child || info.ruled))
            continue;
        const DrawCmd& line = cmds[info.cmd];
        Fragment f;
        f.line = info.cmd;
        f.start = line.start();
        f.end = line.end;
        f.x = held(line.x);
        f.width = held(line.width);
        f.top = held(line.y) - held(line.height);
        f.bottom = held(line.y) + held(line.depth);
        if (info.outer >= 0) {
            const DrawCmd& outer = cmds[_lines[static_cast<size_t>(info.outer)].cmd];
            f.outer = static_cast<int32_t>(_lines[static_cast<size_t>(info.outer)].cmd);
            f.reach_top = held(outer.y) - held(outer.height);
            f.reach_bottom = held(outer.y) + held(outer.depth);
        } else {
            f.reach_top = f.top;
            f.reach_bottom = f.bottom;
        }
        f.first_item = info.first_item;
        f.item_count = info.items;
        f.first_edge = static_cast<uint32_t>(_edges.size());
        // An item's right edge is the next item's left edge, and the last
        // one's is the line's.
        for (uint32_t k = 0; k < info.items; ++k)
            _edges.push_back(held(cmds[_stack[info.first_item + k]].x));
        _edges.push_back(f.x + f.width);
        const Scaled* e = _edges.data() + f.first_edge;
        for (uint32_t k = 1; k < info.items; ++k) {
            if (e[k + 1] < e[k])
                f.edges_sorted = false;
        }
        _fragments.push_back(f);
    }
    _stack.clear();

    // Reading order. Ship-out's lines already are; a stable sort keeps lines
    // that start at the same index (a blank line and the one after it) in the
    // order they came.
    const auto by_start = [](const Fragment& a, const Fragment& b) { return a.start < b.start; };
    if (!std::is_sorted(_fragments.begin(), _fragments.end(), by_start))
        std::stable_sort(_fragments.begin(), _fragments.end(), by_start);

    _reach_end.reserve(_fragments.size());
    for (const Fragment& f : _fragments) {
        _length = f.end > _length ? f.end : _length;
        _reach_end.push_back(_length);
    }
    build_clusters();
}

// Groups the fragments into runs, in reading order, that nothing outside the
// run overlaps vertically. A fragment's extent here is everything it could be
// asked about: its band and its row's band, taken so that it never ends above
// where it begins. A cut after fragment i is good when every extent up to i
// ends at or above where every later one begins. The clusters then come in
// ascending order down the page: each one's `high` is at most the next one's
// `low`.
void SelectionModel::build_clusters()
{
    const size_t n = _fragments.size();
    if (n == 0)
        return;
    const auto low_of = [](const Fragment& f) {
        return std::min(std::min(f.top, f.bottom), std::min(f.reach_top, f.reach_bottom));
    };
    const auto high_of = [](const Fragment& f) {
        return std::max(std::max(f.top, f.bottom), std::max(f.reach_top, f.reach_bottom));
    };
    // The least low among fragments i and after.
    std::vector<Scaled> low_from(n + 1);
    low_from[n] = std::numeric_limits<Scaled>::max();
    for (size_t i = n; i-- > 0;)
        low_from[i] = std::min(low_of(_fragments[i]), low_from[i + 1]);

    Cluster current;
    current.first = 0;
    current.low = low_of(_fragments[0]);
    current.high = high_of(_fragments[0]);
    for (size_t i = 0; i < n; ++i) {
        current.low = std::min(current.low, low_of(_fragments[i]));
        current.high = std::max(current.high, high_of(_fragments[i]));
        ++current.count;
        if (i + 1 == n || current.high <= low_from[i + 1]) {
            _clusters.push_back(current);
            if (i + 1 < n) {
                current = Cluster();
                current.first = static_cast<uint32_t>(i + 1);
                current.low = low_of(_fragments[i + 1]);
                current.high = high_of(_fragments[i + 1]);
            }
        }
    }
}

// --- Hit testing ------------------------------------------------------------------

int SelectionModel::fragment_at(Point point) const
{
    if (_fragments.empty())
        return -1;
    const Scaled x = clamp_coordinate(point.x);
    const Scaled y = clamp_coordinate(point.y);

    // The first cluster that ends below y. Only it can hold a line whose band
    // or whose row contains y.
    const size_t cluster_count = _clusters.size();
    const size_t p = static_cast<size_t>(
        std::upper_bound(_clusters.begin(), _clusters.end(), y,
                         [](Scaled value, const Cluster& c) { return value < c.high; }) -
        _clusters.begin());

    if (p < cluster_count && _clusters[p].low <= y) {
        // Steps 1 and 2 of the rule, over the lines that share this stretch of
        // the page.
        int under = -1;
        Scaled under_by = 0;
        int inside = -1;
        Scaled inside_by = 0;
        const Cluster& cluster = _clusters[p];
        for (uint32_t i = cluster.first; i < cluster.first + cluster.count; ++i) {
            const Fragment& f = _fragments[i];
            const Scaled aside = beside(f, x);
            const bool contains = f.top <= y && y < f.bottom;
            if (aside == 0 && f.reach_top <= y && y < f.reach_bottom) {
                Scaled distance = 0;
                if (!contains) {
                    distance = std::max(f.top - y, y - f.bottom);
                    distance = distance > 0 ? distance : 0;
                }
                if (under < 0 || distance < under_by) {
                    under = static_cast<int>(i);
                    under_by = distance;
                }
            }
            if (contains && (inside < 0 || aside < inside_by)) {
                inside = static_cast<int>(i);
                inside_by = aside;
            }
        }
        if (under >= 0)
            return under;
        if (inside >= 0)
            return inside;
    }

    // Step 3: no band contains y. The nearest line in y, then in x, then the
    // earlier. Clusters are tried outward from where y falls, and a direction
    // is given up once every line further that way is strictly further in y
    // than the best found.
    int best = -1;
    Scaled best_far = 0;
    Scaled best_aside = 0;
    const auto consider = [&](const Cluster& cluster) {
        for (uint32_t i = cluster.first; i < cluster.first + cluster.count; ++i) {
            const Fragment& f = _fragments[i];
            const Scaled far = std::max(f.top - y, y - f.bottom);
            const Scaled aside = beside(f, x);
            const bool nearer = far < best_far || (far == best_far && aside < best_aside);
            const bool same = far == best_far && aside == best_aside;
            if (best < 0 || nearer || (same && static_cast<int>(i) < best)) {
                best = static_cast<int>(i);
                best_far = far;
                best_aside = aside;
            }
        }
    };
    if (p < cluster_count)
        consider(_clusters[p]);
    for (size_t j = p; j-- > 0;) {
        if (best >= 0 && y - _clusters[j].high > best_far)
            break;
        consider(_clusters[j]);
    }
    for (size_t j = p + 1; j < cluster_count; ++j) {
        if (best >= 0 && _clusters[j].low - y > best_far)
            break;
        consider(_clusters[j]);
    }
    return best;
}

Boundary SelectionModel::hit(Point point) const
{
    const int ordinal = fragment_at(point);
    if (ordinal < 0)
        return 0;
    return boundary_in(_fragments[static_cast<size_t>(ordinal)], clamp_coordinate(point.x));
}

// Which of a line's own items sits under x, clamped into the line. The line
// has at least one item.
uint32_t SelectionModel::offset_in(const Fragment& f, Scaled x) const
{
    const Scaled* e = edges(f);
    const uint32_t n = f.item_count;
    if (x < e[0])
        return 0;
    uint32_t offset = n;
    if (f.edges_sorted) {
        offset = static_cast<uint32_t>(std::upper_bound(e + 1, e + n + 1, x) - (e + 1));
    } else {
        for (uint32_t k = 0; k < n; ++k) {
            if (x < e[k + 1]) {
                offset = k;
                break;
            }
        }
    }
    return offset < n ? offset : n - 1;
}

// An x clamped to a boundary inside one line, each item split at its midpoint.
// The midpoint test is the reference's 2 * x < left + right, written as a
// comparison of two distances so that it is exact and cannot overflow.
Boundary SelectionModel::boundary_in(const Fragment& f, Scaled x) const
{
    const uint32_t n = f.item_count;
    if (n == 0)
        return f.start;
    const Scaled* e = edges(f);
    const Boundary* index = indices(f);
    if (x < e[0])
        return index[0];
    uint32_t offset = n;
    if (f.edges_sorted) {
        offset = static_cast<uint32_t>(std::upper_bound(e + 1, e + n + 1, x) - (e + 1));
    } else {
        for (uint32_t k = 0; k < n; ++k) {
            if (x < e[k + 1]) {
                offset = k;
                break;
            }
        }
    }
    if (offset >= n)
        return index[n - 1] + 1;
    const Scaled left = e[offset];
    const Scaled right = e[offset + 1];
    return index[offset] + (x - left < right - x ? 0u : 1u);
}

// --- Ranges -----------------------------------------------------------------------

Selection SelectionModel::select(int64_t anchor, int64_t active) const
{
    const int64_t length = static_cast<int64_t>(_length);
    const int64_t a = anchor < 0 ? 0 : (anchor > length ? length : anchor);
    const int64_t b = active < 0 ? 0 : (active > length ? length : active);
    return Selection{static_cast<Boundary>(a < b ? a : b), static_cast<Boundary>(a < b ? b : a)};
}

Selection SelectionModel::select_at(Point anchor, Point active, Granularity granularity) const
{
    if (granularity == Granularity::CHARACTER)
        return select(hit(anchor), hit(active));
    const Selection first = granularity == Granularity::WORD ? word_at(anchor) : line_at(anchor);
    const Selection second = granularity == Granularity::WORD ? word_at(active) : line_at(active);
    return Selection{std::min(first.start, second.start), std::max(first.end, second.end)};
}

Selection SelectionModel::word_at(Point point) const
{
    const int ordinal = fragment_at(point);
    if (ordinal < 0)
        return Selection{0, 0};
    const Fragment& f = _fragments[static_cast<size_t>(ordinal)];
    if (f.item_count == 0)
        return Selection{f.start, f.start};

    const ItemKind* kind = kinds(f);
    const uint32_t* codepoint = codepoints(f);
    const Boundary* index = indices(f);
    const uint32_t offset = offset_in(f, clamp_coordinate(point.x));
    const Class wanted = classify(kind[offset], codepoint[offset]);
    uint32_t first = offset;
    while (first > 0 && classify(kind[first - 1], codepoint[first - 1]) == wanted)
        --first;
    uint32_t last = offset;
    while (last + 1 < f.item_count && classify(kind[last + 1], codepoint[last + 1]) == wanted)
        ++last;
    return Selection{index[first], index[last] + 1};
}

Selection SelectionModel::line_at(Point point) const
{
    const int ordinal = fragment_at(point);
    if (ordinal < 0)
        return Selection{0, 0};
    const Fragment& f = _fragments[static_cast<size_t>(ordinal)];
    return Selection{f.start, f.end};
}

// The offsets of the items a fragment owns whose indices are in [first, last],
// which are a contiguous run of offsets even when the indices have a hole in
// them. False if there are none.
bool SelectionModel::owned_range(const Fragment& f, int64_t first, int64_t last, uint32_t& low,
                                 uint32_t& high) const
{
    const Boundary* index = indices(f);
    const uint32_t from = bisect_left(index, f.item_count, first);
    const uint32_t past = bisect_right(index, f.item_count, last);
    if (past == 0 || from > past - 1)
        return false;
    low = from;
    high = past - 1;
    return true;
}

// The fragments [first, end) that can have anything to do with a range: those
// that do not end at or before where it starts, nor start at or after where it
// ends. For any fragment outside that, the range asked of owned_range has its
// first index above its last, and the two bisections then agree at every step
// until one goes right where the other goes left, so the answer is "none"
// whatever order the fragment's indices are in. A blank line outside it is not
// strictly inside the range either.
void SelectionModel::candidates(Selection selection, size_t& first, size_t& end) const
{
    first = static_cast<size_t>(
        std::upper_bound(_reach_end.begin(), _reach_end.end(), selection.start) -
        _reach_end.begin());
    end = static_cast<size_t>(
        std::lower_bound(_fragments.begin(), _fragments.end(), selection.end,
                         [](const Fragment& f, Boundary bound) { return f.start < bound; }) -
        _fragments.begin());
    if (end < first)
        end = first;
}

void SelectionModel::rects(Selection selection, std::vector<Rect>& out) const
{
    if (selection.is_empty())
        return;
    size_t first = 0;
    size_t end = 0;
    candidates(selection, first, end);
    for (size_t i = first; i < end; ++i) {
        const Fragment& f = _fragments[i];
        uint32_t low = 0;
        uint32_t high = 0;
        if (!owned_range(f, std::max(selection.start, f.start),
                         static_cast<int64_t>(std::min(selection.end, f.end)) - 1, low, high))
            continue;
        // Fill at either end of the selected stretch is not text and is not
        // highlighted.
        const ItemKind* kind = kinds(f);
        while (low <= high && kind[low] == ItemKind::FILL)
            ++low;
        while (high > low && kind[high] == ItemKind::FILL)
            --high;
        if (low > high || kind[low] == ItemKind::FILL)
            continue;
        // By index and back, as the reference does, so that a fragment whose
        // indices repeat is answered the same way.
        const Boundary* index = indices(f);
        uint32_t from = 0;
        uint32_t to = 0;
        if (!owned_range(f, index[low], index[high], from, to))
            continue;
        const Scaled* e = edges(f);
        out.push_back(Rect{e[from], f.top, e[to + 1] - e[from], f.bottom - f.top});
    }
}

void SelectionModel::text(Selection selection, const TextData& data, std::string& out) const
{
    if (selection.is_empty())
        return;
    Pieces pieces;
    // How many pieces there were when the last character was written.
    size_t written = 0;
    const ByIndex separators(data.separators, data.separator_count);
    const ByIndex atoms(data.atoms, data.atom_count);
    const ByIndex originals(data.originals, data.original_count);
    const Breaks hard_breaks(data.hard_breaks, data.hard_break_count);

    size_t first = 0;
    size_t end = 0;
    candidates(selection, first, end);
    for (size_t i = first; i < end; ++i) {
        const Fragment& f = _fragments[i];
        if (f.item_count == 0) {
            // A blank line. Strictly inside, so a selection that merely ends on
            // one does not pick up a trailing newline.
            if (!pieces.empty() && selection.start < f.start && f.start < selection.end)
                pieces.separate("\n", 1);
            continue;
        }
        uint32_t low = 0;
        uint32_t high = 0;
        if (!owned_range(f, std::max(selection.start, f.start),
                         static_cast<int64_t>(std::min(selection.end, f.end)) - 1, low, high))
            continue;

        const Boundary* index = indices(f);
        const ItemKind* kind = kinds(f);
        const uint32_t* codepoint = codepoints(f);
        bool opening = true;
        // Fill seen since the last character and not yet written: it becomes
        // one space if a character follows it on this line, and nothing if a
        // separator or the end of the line does.
        bool filled = false;
        for (uint32_t offset = low; offset <= high; ++offset) {
            const Boundary at = index[offset];
            // What belongs before this item, if anything was said or follows
            // from where it is. An empty separator is still one.
            const IndexedText* given = separators.find(at);
            bool separated = given != nullptr;
            const char* separator = separated ? given->text : "";
            size_t separator_size = separated ? given->size : 0;
            if (!separated && at == f.start && hard_breaks.has(at)) {
                separated = true;
                separator = "\n";
                separator_size = 1;
            }
            if (!separated && opening && !pieces.empty()) {
                // The first item of this line's share and nothing said what
                // precedes it: a wrap.
                separated = true;
                separator = " ";
                separator_size = 1;
            }
            opening = false;
            if (separated) {
                if (!pieces.empty())
                    pieces.separate(separator, separator_size);
                filled = false;
            }
            if (kind[offset] == ItemKind::FILL) {
                filled = !separated;
                continue;
            }
            if (filled) {
                if (!pieces.empty())
                    pieces.separate(" ", 1);
                filled = false;
            }
            if (kind[offset] == ItemKind::ATOM) {
                // An atom nobody named has nothing to write for it.
                const IndexedText* named = atoms.find(at);
                if (named == nullptr || named->size == 0)
                    continue;
                pieces.append(named->text, named->size);
            } else if (kind[offset] == ItemKind::SPACE) {
                pieces.append(" ", 1);
            } else {
                const IndexedText* original = originals.find(at);
                if (original != nullptr) {
                    if (original->size == 0)
                        continue;
                    pieces.append(original->text, original->size);
                } else {
                    std::string encoded;
                    selection_append_utf8(encoded, codepoint[offset]);
                    pieces.append(encoded.data(), encoded.size());
                }
            }
            written = pieces.count();
        }
    }
    // A separator with no character after it is not between two things.
    pieces.join(written, out);
}

} // namespace ImTypeset::detail
