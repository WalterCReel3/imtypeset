#include "draw.h"

#include "font_loader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ImTypeset::detail
{

namespace
{

// Coordinates in a command are sums formed by ship-out and are far inside 64
// bits, but a command can also be built by hand: these wrap where the plain
// operators would be undefined.
Scaled wrap_add(Scaled a, Scaled b)
{
    return static_cast<Scaled>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

Scaled wrap_sub(Scaled a, Scaled b)
{
    return static_cast<Scaled>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}

// The most quads reserved in a draw list at once. ImDrawList::PrimReserve
// starts a new draw command when a reservation would pass 64k vertices with
// 16-bit indices, and can only do that between reservations. So past 64k
// vertices a draw command ends up to this many quads earlier than it would
// with a reservation for each glyph, which is what one AddText per glyph
// makes: the vertices and the triangles are the same, and where one draw
// command stops and the next starts is not.
constexpr size_t BATCH_QUADS = 1024;

// floor() for the draw loop, which takes two a glyph. Truncating and stepping
// down where that went up is floor exactly for anything an int64 holds, and
// costs a conversion each way where the library call costs a call; outside
// that range, and for a NaN, it is the library's.
inline double floor_px(double value)
{
    if (value > -9.0e15 && value < 9.0e15) {
        const double truncated = static_cast<double>(static_cast<int64_t>(value));
        return truncated > value ? truncated - 1.0 : truncated;
    }
    return std::floor(value);
}

bool draws_nothing(uint32_t codepoint)
{
    // ImDrawList::AddText returns for an empty string, which is what a NUL
    // makes of a one-character one; ImFont::RenderText takes U+000A as a line
    // break and skips U+000D.
    return codepoint == 0 || codepoint == '\n' || codepoint == '\r';
}

// UPSTREAM NOTICE. BufferSink and Drawer::emit reproduce what Dear ImGui
// v1.92.9 writes for a character and for a filled rectangle: the vertex and
// index layout of ImDrawList::PrimRect and PrimRectUV, the reserve, write and
// give-back pattern of ImFont::RenderText, and the order and strictness of its
// tests against the clip rectangle (imgui_draw.cpp). Written to match its
// output and its conventions; Dear ImGui is Copyright (c) 2014-2026 Omar
// Cornut and is distributed under the MIT License.

// Where a sink writes next: the vertex and the index, the number the next
// vertex has, and how many quads have been written since the batch began.
// Held by value in the loop that writes, so that it lives in registers there.
template<class Index>
struct Cursor {
    ImDrawVert* vertex = nullptr;
    Index* index = nullptr;
    unsigned int next = 0;
    int used = 0;

    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
              ImU32 colour)
    {
        vertex[0].pos = ImVec2(x0, y0);
        vertex[0].uv = ImVec2(u0, v0);
        vertex[0].col = colour;
        vertex[1].pos = ImVec2(x1, y0);
        vertex[1].uv = ImVec2(u1, v0);
        vertex[1].col = colour;
        vertex[2].pos = ImVec2(x1, y1);
        vertex[2].uv = ImVec2(u1, v1);
        vertex[2].col = colour;
        vertex[3].pos = ImVec2(x0, y1);
        vertex[3].uv = ImVec2(u0, v1);
        vertex[3].col = colour;
        index[0] = static_cast<Index>(next);
        index[1] = static_cast<Index>(next + 1);
        index[2] = static_cast<Index>(next + 2);
        index[3] = static_cast<Index>(next);
        index[4] = static_cast<Index>(next + 2);
        index[5] = static_cast<Index>(next + 3);
        vertex += 4;
        index += 6;
        next += 4;
        ++used;
    }
};

// Writes quads into space reserved in an ImDrawList, as ImFont::RenderText
// does: through pointers of its own into the reserved space, with the list's
// write position (_VtxWritePtr, _IdxWritePtr, _VtxCurrentIdx, which imgui.h
// marks [Internal]) brought up to date when a batch ends. ImGui's public
// PrimRectUV writes the same quad and keeps that position itself, and is a
// call a glyph that cannot be inlined: measured at 15 % more for a draw into
// a list (docs/performance.md), which is why this does not use it. A rectangle, of
// which there are few, does go through ImGui's PrimRect, because that takes
// the white texel from the shared data of the list it writes to, which need
// not be the current context's and cannot be read from here.
struct ListSink {
    using Position = Cursor<ImDrawIdx>;

    explicit ListSink(ImDrawList* list)
        : draw_list(list)
    {
    }

    Position begin(size_t quads)
    {
        reserved = static_cast<int>(quads);
        draw_list->PrimReserve(reserved * 6, reserved * 4);
        Position at;
        at.vertex = draw_list->_VtxWritePtr;
        at.index = draw_list->_IdxWritePtr;
        // Read after the reservation, which may have started a new command
        // and with it a new vertex numbering.
        at.next = draw_list->_VtxCurrentIdx;
        return at;
    }

    void rect(Position& at, float x0, float y0, float x1, float y1, ImU32 colour)
    {
        sync(at);
        draw_list->PrimRect(ImVec2(x0, y0), ImVec2(x1, y1), colour);
        at.vertex = draw_list->_VtxWritePtr;
        at.index = draw_list->_IdxWritePtr;
        at.next = draw_list->_VtxCurrentIdx;
        ++at.used;
    }

    // Gives back what was reserved and not written.
    void end(const Position& at)
    {
        sync(at);
        const int unused = reserved - at.used;
        reserved = 0;
        if (unused > 0)
            draw_list->PrimUnreserve(unused * 6, unused * 4);
    }

    // The list's write position, set to where the cursor has written up to.
    void sync(const Position& at)
    {
        draw_list->_VtxWritePtr = at.vertex;
        draw_list->_IdxWritePtr = at.index;
        draw_list->_VtxCurrentIdx = at.next;
    }

    ImDrawList* draw_list;
    int reserved = 0;
};

// The same quads into buffers the caller owns.
struct BufferSink {
    using Position = Cursor<unsigned int>;

    explicit BufferSink(DrawBuffers& buffers)
        : out(buffers)
        , white(ImGui::GetCurrentContext() != nullptr ? ImGui::GetFontTexUvWhitePixel()
                                                      : ImVec2(0.0f, 0.0f))
    {
    }

    Position begin(size_t quads)
    {
        const int count = static_cast<int>(quads);
        vertex_base = out.vertices.Size;
        index_base = out.indices.Size;
        // ImVector grows geometrically and keeps its capacity, so a frame no
        // larger than the last allocates nothing.
        out.vertices.resize(vertex_base + count * 4);
        out.indices.resize(index_base + count * 6);
        Position at;
        at.vertex = out.vertices.Data + vertex_base;
        at.index = out.indices.Data + index_base;
        at.next = static_cast<unsigned int>(vertex_base);
        return at;
    }

    // With the current context's white texel: there is no list to ask.
    void rect(Position& at, float x0, float y0, float x1, float y1, ImU32 colour)
    {
        at.quad(x0, y0, x1, y1, white.x, white.y, white.x, white.y, colour);
    }

    void end(const Position& at)
    {
        out.vertices.resize(vertex_base + at.used * 4);
        out.indices.resize(index_base + at.used * 6);
    }

    DrawBuffers& out;
    ImVec2 white;
    int vertex_base = 0;
    int index_base = 0;
};

// How many quads an op can come to. The overlay's outlines are this file's own
// rectangles, one pixel thick, and not ImGui's AddRect and AddLine, whose
// geometry is anti-aliased, depends on the list's flags and is not the same
// from one version of ImGui to the next.
constexpr size_t MOST_QUADS_PER_OP = 4;

// The frame of a RECT op: the pixels of the rectangle that touch its edge. A
// rectangle two pixels or less across either way is all edge.
template<class Sink>
void write_frame(Sink& sink, typename Sink::Position& at, const DrawOp& op)
{
    if (!(op.x1 > op.x0 && op.y1 > op.y0))
        return;
    if (!(op.x1 - op.x0 > 2.0f && op.y1 - op.y0 > 2.0f)) {
        sink.rect(at, op.x0, op.y0, op.x1, op.y1, op.colour);
        return;
    }
    sink.rect(at, op.x0, op.y0, op.x1, op.y0 + 1.0f, op.colour);
    sink.rect(at, op.x0, op.y1 - 1.0f, op.x1, op.y1, op.colour);
    sink.rect(at, op.x0, op.y0 + 1.0f, op.x0 + 1.0f, op.y1 - 1.0f, op.colour);
    sink.rect(at, op.x1 - 1.0f, op.y0 + 1.0f, op.x1, op.y1 - 1.0f, op.colour);
}

// A LINE_SEGMENT op, which is horizontal: the row of pixels below its y.
template<class Sink>
void write_segment(Sink& sink, typename Sink::Position& at, const DrawOp& op)
{
    if (op.x1 > op.x0)
        sink.rect(at, op.x0, op.y0, op.x1, op.y0 + 1.0f, op.colour);
}

} // namespace

// --- The pure helpers ---------------------------------------------------------------

double snap_px(double value)
{
    return floor_px(value + 0.5);
}

void snap_span(double from, double to, double* lo, double* hi)
{
    *lo = snap_px(from);
    *hi = snap_px(to);
    if (*hi <= *lo && to > from)
        *hi = *lo + 1.0;
}

double baked_size_px(double size_px, double ascent, double descent, double em)
{
    if (em <= 0.0)
        return size_px;
    return size_px * (ascent + descent) / em;
}

float device_coordinate(float origin, Scaled length, double px_per_inch)
{
    return static_cast<float>(static_cast<double>(origin) + px_from_sp(length, px_per_inch));
}

// --- Lines ----------------------------------------------------------------------------

void index_lines(const DrawCmd* cmds, size_t count, LineIndex& out)
{
    out.clear();
    if (cmds == nullptr)
        return;
    LineSpan span;
    bool open = false;       // a span is being built
    bool in_line = false;    // and it began with a LINE
    bool has_extent = false; // and something in it has stated a vertical extent
    bool uncovered = false;  // and it holds an item no line covers
    uint32_t line_end = 0;   // the index range of the LINE it began with ends here

    const auto extend = [&](Scaled a, Scaled b) {
        const Scaled top = a < b ? a : b;
        const Scaled bottom = a < b ? b : a;
        if (!has_extent) {
            span.top = top;
            span.bottom = bottom;
            has_extent = true;
            return;
        }
        if (top < span.top)
            span.top = top;
        if (bottom > span.bottom)
            span.bottom = bottom;
    };
    const auto close = [&](size_t end) {
        if (!open)
            return;
        span.end = static_cast<uint32_t>(end);
        span.bounded = has_extent && !uncovered;
        out.spans.push_back(span);
    };
    const auto start = [&](size_t first, bool with_line) {
        span = LineSpan();
        span.first = static_cast<uint32_t>(first);
        open = true;
        in_line = with_line;
        has_extent = false;
        uncovered = false;
    };

    const size_t limit = count < 0xFFFFFFFFu ? count : 0xFFFFFFFFu;
    for (size_t i = 0; i < limit; ++i) {
        const DrawCmd& cmd = cmds[i];
        if (cmd.type == CmdType::LINE) {
            const bool nested = open && in_line && cmd.index < line_end;
            if (!nested) {
                close(i);
                start(i, true);
                line_end = cmd.end;
            }
            extend(wrap_sub(cmd.y, cmd.height), wrap_add(cmd.y, cmd.depth));
            continue;
        }
        if (!open)
            start(i, false);
        if (cmd.type == CmdType::RULE_RECT)
            extend(cmd.y, wrap_add(cmd.y, cmd.height));
        else if (!in_line)
            uncovered = true;
    }
    close(limit);

    for (size_t i = 1; i < out.spans.size() && out.sorted; ++i) {
        const LineSpan& now = out.spans[i];
        if (!now.bounded) {
            out.sorted = false;
        } else if (i > 1 || out.spans[0].bounded) {
            const LineSpan& before = out.spans[i - 1];
            out.sorted = now.top >= before.top && now.bottom >= before.bottom;
        }
    }
}

Scaled ink_overhang(const DrawCmd* cmds, size_t count)
{
    if (cmds == nullptr)
        return 0;
    Scaled glyph = 0;
    Scaled atom = 0;
    for (size_t i = 0; i < count; ++i) {
        const DrawCmd& cmd = cmds[i];
        if (cmd.type == CmdType::GLYPH)
            glyph = std::max(glyph, cmd.font().size());
        else if (cmd.type == CmdType::ATOM)
            atom = std::max(atom, std::max(cmd.height, cmd.depth));
    }
    return add_saturating(atom, add_saturating(glyph, glyph));
}

// --- ImGui as the binder ------------------------------------------------------------

class Drawer::ImGuiBinder : public FontBinder
{
public:
    struct KeyState {
        // The face's font; for a face with none bound, whatever font was
        // current when the key was first drawn in. `bound` says which, and is
        // what decides whether a codepoint is asked for before it is drawn.
        ImFont* font = nullptr;
        bool bound = false;
        const FaceData* data = nullptr;
        ImFontBaked* baked = nullptr;
        FontSpec spec;
        // The size a glyph is drawn at, and that size over the baked one:
        // exactly 1 for a bound face.
        float size = 0.0f;
        float scale = 1.0f;
    };

    void configure(const FaceFonts& fonts, MetricsResolver metrics, double px_per_inch)
    {
        _fonts = &fonts;
        _metrics = metrics;
        _px_per_inch = px_per_inch;
    }

    void begin() override
    {
        keys.clear();
        atlas_codepoints.clear();
        boxes.clear();
    }

    Binding bind(uint32_t key_slot, FontKey key) override
    {
        if (keys.size() <= key_slot)
            keys.resize(static_cast<size_t>(key_slot) + 1);
        KeyState& state = keys[key_slot];
        if (!parse_font_key(key, &state.spec))
            return Binding::UNKNOWN;
        const FaceFont& bound = _fonts->faces[static_cast<size_t>(state.spec.face)];
        state.font = bound.font;
        state.bound = bound.font != nullptr;
        state.data = bound.data;
        return state.bound ? Binding::BOUND : Binding::UNBOUND;
    }

    bool has_glyph(uint32_t glyph_slot, uint32_t key_slot, uint32_t codepoint) override
    {
        if (atlas_codepoints.size() <= glyph_slot)
            atlas_codepoints.resize(static_cast<size_t>(glyph_slot) + 1, 0);
        const KeyState& state = keys[key_slot];
        // Zero is both a codepoint a font may map and atlas_codepoint's answer
        // for one the atlas cannot be asked for.
        uint32_t asked = codepoint;
        if (codepoint != 0 && state.data != nullptr) {
            asked = atlas_codepoint(*state.data, codepoint);
            if (asked == 0)
                return false;
        }
        if (asked > IM_UNICODE_CODEPOINT_MAX)
            return false;
        atlas_codepoints[glyph_slot] = asked;
        // With no bound face there is nothing to ask: the glyph is drawn in
        // whatever font is current, as that font's fallback if it has none.
        // Not `font == nullptr`: bake() fills that in for an unbound key, and
        // the answer would then depend on which of its glyphs came first.
        if (!state.bound)
            return true;
        return state.font->IsGlyphInFont(static_cast<ImWchar>(asked));
    }

    bool bake(uint32_t key_slot, FontKey key, float* size, float* ascent) override
    {
        KeyState& state = keys[key_slot];
        if (!state.bound) {
            if (ImGui::GetCurrentContext() == nullptr)
                return false;
            // What ImDrawList::AddText without a font draws in.
            state.font = ImGui::GetFont();
            state.size = ImGui::GetFontSize();
            if (state.font == nullptr || !(state.size >= 0.5f && state.size <= MAX_BAKED_SIZE_PX))
                return false;
            state.baked = state.font->GetFontBaked(state.size);
            if (state.baked == nullptr || !(state.baked->Size > 0.0f))
                return false;
            state.scale = state.size / state.baked->Size;
        } else {
            // Our em size, corrected to the pixel height ImGui scales by, then
            // rounded by ImGui itself.
            const double nominal = px_from_sp(state.spec.size, _px_per_inch);
            double requested = nominal;
            if (const FontMetrics* ours = _metrics(key))
                requested = baked_size_px(nominal, ours->ascent(), ours->descent(), ours->em());
            if (!(requested >= 0.5 && requested <= static_cast<double>(MAX_BAKED_SIZE_PX)))
                return false;
            state.baked = state.font->GetFontBaked(static_cast<float>(requested));
            if (state.baked == nullptr)
                return false;
            // The baked size and not the one asked for: the same raster, drawn
            // one texel to one pixel.
            state.size = state.baked->Size;
            state.scale = 1.0f;
        }
        *size = state.size;
        *ascent = state.baked->Ascent;
        return true;
    }

    // Loads every glyph the ops will draw, then copies out what a quad needs
    // of each. Two passes because loading one glyph can move the others:
    // ImGui packs glyphs into the atlas on demand, and both the glyph array
    // and the texture coordinates in it change as it grows. Nothing is
    // reserved in a draw list until this has returned, for the reason
    // ImFont::RenderText retries when a glyph load changes the texture under
    // it. A glyph no op will draw — its colour has no alpha, its codepoint
    // draws nothing, its pen is outside the clip rectangle vertically — is
    // not loaded, as ImGui would not have loaded it.
    void load(const Drawer& drawer, const ImVec4& clip)
    {
        const std::vector<Glyph>& glyphs = drawer.glyphs();
        boxes.assign(glyphs.size(), Box());
        _wanted.assign(glyphs.size(), 0);
        size_t unwanted = glyphs.size();
        for (const DrawOp& op : drawer.ops()) {
            if (unwanted == 0)
                break;
            if (op.type != OpType::TEXT || _wanted[op.glyph] != 0)
                continue;
            const Glyph& glyph = glyphs[op.glyph];
            if ((op.colour & IM_COL32_A_MASK) == 0 || draws_nothing(glyph.codepoint) ||
                outside_vertically(op.y0, keys[glyph.key].size, clip))
                continue;
            _wanted[op.glyph] = 1;
            --unwanted;
        }
        for (int pass = 0; pass < 2; ++pass) {
            for (size_t i = 0; i < glyphs.size(); ++i) {
                if (_wanted[i] == 0)
                    continue;
                const KeyState& key = keys[glyphs[i].key];
                const ImFontGlyph* found =
                    key.baked->FindGlyph(static_cast<ImWchar>(atlas_codepoints[i]));
                if (pass == 0 || found == nullptr || !found->Visible)
                    continue;
                // The offsets are scaled here, in float, as ImFont::RenderText
                // scales them before adding the pen. For a bound face the
                // scale is 1.
                Box& box = boxes[i];
                box.x0 = found->X0 * key.scale;
                box.y0 = found->Y0 * key.scale;
                box.x1 = found->X1 * key.scale;
                box.y1 = found->Y1 * key.scale;
                box.u0 = found->U0;
                box.v0 = found->V0;
                box.u1 = found->U1;
                box.v1 = found->V1;
                box.size = key.size;
                box.visible = true;
                box.coloured = found->Colored != 0;
            }
        }
    }

    // ImFont::RenderText's two tests on a line of text: the pen below the
    // clip rectangle, or the whole cell above it. In float, as it does them.
    static bool outside_vertically(float pen_y, float size, const ImVec4& clip)
    {
        return pen_y > clip.w || pen_y + size < clip.y;
    }

    // What a quad needs of one glyph, beside its pen. `visible` is false for
    // a glyph with an empty bitmap and for one that was not loaded.
    struct Box {
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
        float size = 0.0f;
        bool visible = false;
        bool coloured = false;
    };

    std::vector<KeyState> keys;
    std::vector<uint32_t> atlas_codepoints; // by glyph slot
    std::vector<Box> boxes;                 // by glyph slot

private:
    const FaceFonts* _fonts = nullptr;
    MetricsResolver _metrics;
    double _px_per_inch = DEFAULT_PX_PER_INCH;
    std::vector<uint8_t> _wanted;
};

// --- Placement ------------------------------------------------------------------------

// The anchor a run of glyphs is snapped relative to, and the running state:
// the exact x of the glyph placed last and the screen x it was placed at.
struct Drawer::Span {
    bool open = false;
    Scaled y = 0;
    double ascent = 0.0;
    double top = 0.0;
    Scaled prev_x = 0;
    double prev_pen = 0.0;
    // The word of the span, once it has one: its glyphs that are not a
    // formula's, which are its last. The op of the first of them, its x, and
    // the screen x it was placed at.
    bool word = false;
    size_t word_op = 0;
    Scaled word_x = 0;
    double word_pen = 0.0;
};

Drawer::Drawer()
    : _binder(new ImGuiBinder())
{
}

Drawer::~Drawer()
{
    delete _binder;
}

uint32_t Drawer::key_slot(FontKey key, FontBinder& binder)
{
    // A run of text is one key many times over, and a document has few.
    if (_last_key < _keys.size() && _keys[_last_key].key == key)
        return _last_key;
    for (size_t i = 0; i < _keys.size(); ++i) {
        if (_keys[i].key == key) {
            _last_key = static_cast<uint32_t>(i);
            return _last_key;
        }
    }
    const uint32_t slot = static_cast<uint32_t>(_keys.size());
    Key added;
    added.key = key;
    added.binding = binder.bind(slot, key);
    _keys.push_back(added);
    _latin.resize(_latin.size() + 256, 0);
    _last_key = slot;
    return slot;
}

uint32_t Drawer::glyph_slot(uint32_t key, uint32_t codepoint, FontBinder& binder)
{
    uint32_t* cell = nullptr;
    if (codepoint < 256) {
        cell = &_latin[static_cast<size_t>(key) * 256 + codepoint];
    } else {
        // At most half full, so a probe always ends.
        if (_other_count * 2 >= _others.size()) {
            std::vector<Other> old;
            old.swap(_others);
            _others.resize(old.empty() ? 256 : old.size() * 2);
            for (const Other& entry : old) {
                if (entry.slot == 0)
                    continue;
                size_t at = (entry.id * 0x9E3779B97F4A7C15ull >> 32) & (_others.size() - 1);
                while (_others[at].slot != 0)
                    at = (at + 1) & (_others.size() - 1);
                _others[at] = entry;
            }
        }
        const uint64_t id = (static_cast<uint64_t>(key) << 32) | codepoint;
        size_t at = (id * 0x9E3779B97F4A7C15ull >> 32) & (_others.size() - 1);
        while (_others[at].slot != 0 && _others[at].id != id)
            at = (at + 1) & (_others.size() - 1);
        if (_others[at].slot == 0) {
            _others[at].id = id;
            ++_other_count;
        }
        cell = &_others[at].slot;
    }
    if (*cell != 0)
        return *cell - 1;
    const uint32_t slot = static_cast<uint32_t>(_glyphs.size());
    *cell = slot + 1; // before the binder is asked: `cell` may not outlive it
    Glyph added;
    added.key = key;
    added.codepoint = codepoint;
    added.present = _keys[key].binding != FontBinder::Binding::UNKNOWN &&
                    binder.has_glyph(slot, key, codepoint);
    _glyphs.push_back(added);
    return slot;
}

void Drawer::push_area(OpType type, Scaled x, Scaled y, Scaled width, Scaled height, ImU32 colour,
                       const DrawArgs& args)
{
    // The two corners pass through float32, as the reference's ImVec2s do,
    // and are snapped from there; `snap_span` compares the float32 values.
    const double near_x = device_coordinate(args.origin.x, x, args.px_per_inch);
    const double near_y = device_coordinate(args.origin.y, y, args.px_per_inch);
    const double far_x = device_coordinate(args.origin.x, wrap_add(x, width), args.px_per_inch);
    const double far_y = device_coordinate(args.origin.y, wrap_add(y, height), args.px_per_inch);
    double left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;
    snap_span(near_x, far_x, &left, &right);
    snap_span(near_y, far_y, &top, &bottom);
    DrawOp op;
    op.type = type;
    op.x0 = static_cast<float>(left);
    op.y0 = static_cast<float>(top);
    op.x1 = static_cast<float>(right);
    op.y1 = static_cast<float>(bottom);
    op.colour = colour;
    _ops.push_back(op);
}

// Holds a word found `drift` pixels wider than it was laid out, which is more
// than WORD_WIDTH_BOUND_PX either way, to its laid-out width
// (DrawRules::word_width_held), and answers how far its last glyph was moved.
// The word's ops are those from `word_op` to the last in `_ops`, all of them
// glyphs, each with the rounding of the gap before it in `x1`.
//
// As many gaps give as it takes whole pixels to bring the drift within the
// bound: those rounded furthest the way the word is out, the earliest of
// equals first. The roundings that way add up to more than the drift is past
// the bound, so there are that many. A word is nearly always out by one
// pixel, and one pass finds its gap. For a word out by more, the smallest
// rounding that gives is found a byte at a time from the top of its bits,
// which order as the roundings do: four passes over the word and a fifth to
// move it, whatever the roundings are and in no memory but the word's own
// ops. The work grows with the word's length and not with its length times
// its drift, which for a word of a megabyte is the difference between a
// frame and minutes.
double Drawer::settle_word(size_t word_op, double drift)
{
    const double sign = drift > 0.0 ? 1.0 : -1.0;
    const float shift = static_cast<float>(sign);
    const size_t end = _ops.size();
    // Exact: the drift is past a half, and a half comes off it.
    const double wanted = std::ceil(std::fabs(drift) - WORD_WIDTH_BOUND_PX);
    if (!(wanted > 1.0)) {
        float most = 0.0f;
        size_t give = 0;
        for (size_t k = word_op + 1; k < end; ++k) {
            const float rounded = shift * _ops[k].x1;
            if (rounded > most) {
                most = rounded;
                give = k;
            }
        }
        if (give == 0)
            return 0.0;
        // Rounded the other way, and x1 says so.
        _ops[give].x1 -= shift;
        for (size_t k = give; k < end; ++k)
            _ops[k].x0 -= shift;
        return -sign;
    }

    // A rounding that can give is above zero, and of two such floats the
    // larger has the larger bits.
    const auto bits_of = [](float rounded) {
        uint32_t bits = 0;
        std::memcpy(&bits, &rounded, sizeof bits);
        return bits;
    };
    size_t able = 0;
    for (size_t k = word_op + 1; k < end; ++k)
        able += shift * _ops[k].x1 > 0.0f ? 1 : 0;
    if (able == 0)
        return 0.0;
    // Every gap above `least` gives, and the first `equals` of those at it.
    size_t count = able;
    uint32_t least = 0;
    size_t equals = able;
    if (wanted < static_cast<double>(able)) {
        count = static_cast<size_t>(wanted);
        uint32_t mask = 0;
        equals = count;
        for (int place = 24; place >= 0; place -= 8) {
            // Of the roundings that agree with `least` in the bytes settled
            // so far, of which there are at least `equals`, how many have
            // each value of the next byte down.
            size_t having[256] = {};
            for (size_t k = word_op + 1; k < end; ++k) {
                const float rounded = shift * _ops[k].x1;
                if (!(rounded > 0.0f))
                    continue;
                const uint32_t bits = bits_of(rounded);
                if ((bits & mask) == least)
                    ++having[(bits >> place) & 0xFFu];
            }
            uint32_t byte = 255;
            while (having[byte] < equals) {
                equals -= having[byte];
                --byte;
            }
            least |= byte << place;
            mask |= 0xFFu << place;
        }
    }
    // From each gap that gives, every glyph after it moves a pixel more.
    float by = 0.0f;
    for (size_t k = word_op + 1; k < end; ++k) {
        const float rounded = shift * _ops[k].x1;
        if (rounded > 0.0f) {
            const uint32_t bits = bits_of(rounded);
            bool gives = bits > least;
            if (bits == least && equals > 0) {
                --equals;
                gives = true;
            }
            if (gives) {
                _ops[k].x1 -= shift;
                by += shift;
            }
        }
        _ops[k].x0 -= by;
    }
    return -sign * static_cast<double>(count);
}

void Drawer::place_commands(const DrawCmd* cmds, size_t count, ImU32 ink, const DrawArgs& args,
                            FontBinder& binder, DrawStats& stats)
{
    const double px_per_inch = args.px_per_inch;
    // px_from_sp with its resolution ratio formed once for the list: the same
    // three operations in the same order, so the same double.
    const double ratio = px_per_inch / PT_PER_INCH;
    const auto px = [ratio](Scaled length) {
        return static_cast<double>(length) * ratio / static_cast<double>(UNITY);
    };
    Span span;
    // The key of the glyph before this one, once there has been one. Any bits
    // may be a key: a command list can be built by hand.
    bool has_key = false;
    Scaled key_bits = 0;
    uint32_t key_index = 0;
    // The formula being placed whole, if any (DrawRules::formula_placed_whole):
    // the commands from its ATOM to the next glue, atom or line, to the first
    // glyph that does not carry its index, or to a rule that lies wholly to
    // one side of its box. Its glyphs and rules are placed from
    // `formula_pen`, the pixel the left edge of its box (`formula_x`) was put
    // on. Never set under the reference's rules.
    bool in_formula = false;
    uint32_t formula_index = 0;
    Scaled formula_x = 0;
    Scaled formula_width = 0;
    double formula_pen = 0.0;
    const bool hold_words = args.rules.word_width_held;
    const bool lead_words = args.rules.word_spaces_led;
    // The leader (DrawRules::word_spaces_led): what the next word or formula
    // on the line is placed from, as it is in the commands and where it
    // went. The last glyph of the word before it; or, of the formula before
    // it, the part furthest to the right in the layout, a glyph by its
    // origin and a rule by its right-hand end, and the left edge of the box
    // while it has none further. It stays the leader across set glue and
    // across the start of a formula, and nothing else. While a formula is
    // being placed the two are kept whether or not `leads` is set: text
    // straight after it is placed from them.
    bool leads = false;
    Scaled lead_x = 0;
    double lead_pen = 0.0;
    // Where something whose own place is `place` goes when it is led: the
    // leader's pixel plus the distance between them rounded once, and no
    // further from its own place than the pixel on either side of it.
    const auto led_to = [&](Scaled x, double place) {
        const double led = lead_pen + snap_px(px(wrap_sub(x, lead_x)));
        const double low = floor_px(place);
        const double high = low < place ? low + 1.0 : low;
        return std::min(std::max(led, low), high);
    };
    // Ends the span, holding its word to its width; a span that ended in a
    // word makes that word's last glyph the leader. What ended it decides
    // whether there is a leader after it at all.
    const auto end_span = [&](bool keeps_leader) {
        if (span.open) {
            span.open = false;
            if (span.word) {
                if (hold_words) {
                    const double drift =
                        span.prev_pen - span.word_pen - px(wrap_sub(span.prev_x, span.word_x));
                    if (std::fabs(drift) > WORD_WIDTH_BOUND_PX)
                        span.prev_pen += settle_word(span.word_op, drift);
                }
                leads = lead_words;
                lead_x = span.prev_x;
                lead_pen = span.prev_pen;
            }
        }
        if (!keeps_leader)
            leads = false;
    };
    for (size_t i = 0; i < count; ++i) {
        const DrawCmd& cmd = cmds[i];
        switch (cmd.type) {
        case CmdType::GLYPH: {
            ImU32 colour = ink;
            if (args.ink_count != 0) {
                // The last range starting at or before this glyph (Python's
                // bisect_right, less one); the ranges do not overlap, so it is
                // the only one that can hold it.
                size_t low = 0;
                size_t high = args.ink_count;
                while (low < high) {
                    const size_t middle = low + (high - low) / 2;
                    if (cmd.index < args.inks[middle].start)
                        high = middle;
                    else
                        low = middle + 1;
                }
                if (low > 0 && cmd.index < args.inks[low - 1].end)
                    colour = args.inks[low - 1].colour;
            }
            // A run of text is one key many times over, and mostly codepoints
            // it has already met: both are answered here, and the functions
            // are for the first sight of each.
            if (!has_key || cmd.a != key_bits) {
                key_index = key_slot(FontKey{static_cast<uint64_t>(cmd.a)}, binder);
                key_bits = cmd.a;
                has_key = true;
            }
            const uint32_t seen = cmd.codepoint < 256
                                      ? _latin[static_cast<size_t>(key_index) * 256 + cmd.codepoint]
                                      : 0;
            const uint32_t glyph_index =
                seen != 0 ? seen - 1 : glyph_slot(key_index, cmd.codepoint, binder);
            Key& key = _keys[key_index];
            // A codepoint the face has no raster for is counted and not drawn.
            // It neither opens a span nor closes one: it has no anchor to
            // offer, and it is not glue, so the run is still rigid across it.
            if (!_glyphs[glyph_index].present) {
                ++stats.missing;
                break;
            }
            if (!key.asked) {
                key.asked = true;
                key.drawable = binder.bake(key_index, key.key, &key.size, &key.ascent);
            }
            if (!key.drawable) {
                ++stats.missing;
                break;
            }
            const double ascent = static_cast<double>(key.ascent);
            if (!span.open) {
                const double x = device_coordinate(args.origin.x, cmd.x, px_per_inch);
                const double y = device_coordinate(args.origin.y, cmd.y, px_per_inch);
                span.open = true;
                span.y = cmd.y;
                span.ascent = ascent;
                span.top = snap_px(y - ascent);
                // The word starts here unless this is a glyph of a formula,
                // which is placed below from the formula's edge. A word is
                // placed from the leader if there is one.
                span.word = !in_formula || cmd.index != formula_index;
                span.prev_pen = span.word && leads ? led_to(cmd.x, x) : snap_px(x);
                span.prev_x = cmd.x;
                span.word_op = _ops.size();
                span.word_x = cmd.x;
                span.word_pen = span.prev_pen;
            }
            // Vertically: the offset from the anchor, snapped absolutely. A
            // face change inside a span moves the top of the cell by the
            // difference of the two baked ascents.
            // On the anchor's own baseline in the anchor's own face, which is
            // nearly every glyph, the offset is snap_px(0 - 0) and is not
            // worked out.
            double rows = 0.0;
            if (cmd.y != span.y || ascent != span.ascent) {
                rows = snap_px(px(wrap_sub(cmd.y, span.y)) - (ascent - span.ascent));
            }
            const double pen_y = span.top + rows;
            // Horizontally: the gap from the glyph placed before, rounded
            // once, added to where that glyph went. The anchor takes this
            // path with a gap of zero. A glyph of a formula placed whole
            // takes its gap from the formula's edge, and where that went,
            // instead; a formula's glyphs carry its atom's index, and the
            // first that does not is text again.
            if (in_formula) {
                if (cmd.index == formula_index) {
                    span.prev_x = formula_x;
                    span.prev_pen = formula_pen;
                    if (cmd.x > lead_x) {
                        lead_x = cmd.x;
                        lead_pen = formula_pen + snap_px(px(wrap_sub(cmd.x, formula_x)));
                    }
                } else {
                    in_formula = false;
                    if (!span.word) {
                        // Text straight after a formula's glyphs: the word
                        // of the span starts here. Its first glyph is placed
                        // from the formula's rightmost part, which need not
                        // be the glyph the formula shipped last, by the gap
                        // rounded once; and, when words are led, is kept
                        // beside its own place as a led word is. The gap
                        // before it is not one of the word's.
                        span.prev_pen = leads ? led_to(cmd.x, device_coordinate(args.origin.x,
                                                                                cmd.x, px_per_inch))
                                              : lead_pen + snap_px(px(wrap_sub(cmd.x, lead_x)));
                        span.prev_x = cmd.x;
                        span.word = true;
                        span.word_op = _ops.size();
                        span.word_x = cmd.x;
                        span.word_pen = span.prev_pen;
                    }
                }
            }
            const double step = px(wrap_sub(cmd.x, span.prev_x));
            const double gap = snap_px(step);
            const double pen_x = span.prev_pen + gap;
            span.prev_x = cmd.x;
            span.prev_pen = pen_x;

            DrawOp op;
            op.type = OpType::TEXT;
            op.glyph = glyph_index;
            op.x0 = static_cast<float>(pen_x);
            op.y0 = static_cast<float>(pen_y);
            op.colour = colour;
            op.index = cmd.index;
            // What settle_word reads: how far the gap before this glyph was
            // rounded up. Kept with the op so that a word found too wide or
            // too narrow is put right from its ops alone.
            if (hold_words)
                op.x1 = static_cast<float>(gap - step);
            _ops.push_back(op);
            ++stats.glyphs;
            break;
        }
        case CmdType::RULE_RECT:
            ++stats.rules;
            // A formula's rules lie in its box. One wholly to the left or to
            // the right of it is not the formula's, whatever follows it:
            // ship-out makes no such list, and a list built by hand can be.
            if (in_formula && (cmd.x > wrap_add(formula_x, formula_width) ||
                               wrap_add(cmd.x, cmd.width) < formula_x))
                in_formula = false;
            if (in_formula) {
                // Its two ends as a glyph of the formula is placed, and an
                // area keeps its ink. Its top and bottom from the baseline
                // the formula's first glyph was put on, which is where that
                // glyph's cell went plus the cell's ascent; a rule shipped
                // before any glyph of its formula has none to take and is
                // snapped by its own two edges. Either way the span goes on.
                DrawOp op;
                op.type = OpType::RECT_FILLED;
                op.colour = args.palette.rule;
                const Scaled far = wrap_add(cmd.x, cmd.width);
                double left = formula_pen + snap_px(px(wrap_sub(cmd.x, formula_x)));
                double right = formula_pen + snap_px(px(wrap_sub(far, formula_x)));
                if (far > lead_x) {
                    lead_x = far;
                    lead_pen = right;
                }
                if (right <= left && cmd.width > 0)
                    right = left + 1.0;
                double top = 0.0, bottom = 0.0;
                if (span.open) {
                    const double baseline = span.top + span.ascent;
                    snap_span(baseline + px(wrap_sub(cmd.y, span.y)),
                              baseline + px(wrap_sub(wrap_add(cmd.y, cmd.height), span.y)), &top,
                              &bottom);
                } else {
                    snap_span(
                        device_coordinate(args.origin.y, cmd.y, px_per_inch),
                        device_coordinate(args.origin.y, wrap_add(cmd.y, cmd.height), px_per_inch),
                        &top, &bottom);
                }
                op.x0 = static_cast<float>(left);
                op.x1 = static_cast<float>(right);
                op.y0 = static_cast<float>(top);
                op.y1 = static_cast<float>(bottom);
                _ops.push_back(op);
                break;
            }
            end_span(false);
            push_area(OpType::RECT_FILLED, cmd.x, cmd.y, cmd.width, cmd.height,
                      args.rule_between != 0 ? args.rule_between : args.palette.rule, args);
            break;
        case CmdType::SET_GLUE:
        case CmdType::ATOM:
            in_formula = cmd.type == CmdType::ATOM && args.rules.formula_placed_whole;
            end_span(cmd.type == CmdType::SET_GLUE || in_formula);
            if (in_formula) {
                // The edge of its box is placed as a word's first glyph is:
                // from the leader if there is one, and on the pixel nearest
                // its own place if not. Then it is the leader, until a part
                // of the formula stands further right.
                const double x = device_coordinate(args.origin.x, cmd.x, px_per_inch);
                formula_index = cmd.index;
                formula_x = cmd.x;
                formula_width = cmd.width;
                formula_pen = leads ? led_to(cmd.x, x) : snap_px(x);
                leads = lead_words;
                lead_x = cmd.x;
                lead_pen = formula_pen;
            }
            if (args.debug && cmd.width != cmd.a) {
                push_area(OpType::RECT_FILLED, cmd.x, wrap_sub(cmd.y, cmd.height), cmd.width,
                          wrap_add(cmd.height, cmd.depth),
                          cmd.width > cmd.a ? args.palette.debug_stretched
                                            : args.palette.debug_shrunk,
                          args);
            }
            break;
        case CmdType::LINE:
            end_span(false);
            in_formula = false;
            if (args.debug) {
                push_area(OpType::RECT, cmd.x, wrap_sub(cmd.y, cmd.height), cmd.width,
                          wrap_add(cmd.height, cmd.depth), args.palette.debug_box, args);
                push_area(OpType::LINE_SEGMENT, cmd.x, cmd.y, cmd.width, 0,
                          args.palette.debug_baseline, args);
            }
            break;
        default:
            // Not a command type: a buffer that was not built by ship. It
            // draws nothing and ends the span, as anything but a glyph does.
            end_span(false);
            in_formula = false;
            break;
        }
    }
    end_span(false);
}

DrawStats Drawer::place(const DrawRun* runs, size_t run_count, const DrawArgs& args,
                        FontBinder& binder, const ImVec4* cull_clip)
{
    _ops.clear();
    _keys.clear();
    _glyphs.clear();
    _latin.clear();
    if (_other_count != 0) {
        std::fill(_others.begin(), _others.end(), Other());
        _other_count = 0;
    }
    _last_key = 0;
    binder.begin();

    DrawStats stats;
    for (size_t i = 0; i < args.fill_count; ++i) {
        const Fill& fill = args.fills[i];
        push_area(OpType::RECT_FILLED, fill.rect.x, fill.rect.y, fill.rect.width, fill.rect.height,
                  fill.colour, args);
    }
    for (size_t i = 0; i < args.highlight_count; ++i) {
        const Rect& rect = args.highlights[i];
        push_area(OpType::RECT_FILLED, rect.x, rect.y, rect.width, rect.height,
                  args.palette.highlight, args);
    }

    for (size_t r = 0; r < run_count; ++r) {
        const DrawRun& run = runs[r];
        if (run.cmds == nullptr || run.count == 0)
            continue;
        if (!args.cull_lines || cull_clip == nullptr || run.lines == nullptr) {
            place_commands(run.cmds, run.count, run.ink, args, binder, stats);
            continue;
        }
        // A line is skipped only when its box, grown by the margin, misses the
        // clip rectangle. Each span that is kept starts with its LINE, which
        // ends any rigid span, so skipping one changes nothing in the next.
        const std::vector<LineSpan>& spans = run.lines->spans;
        const double origin_y = static_cast<double>(args.origin.y);
        const double reach =
            CULL_MARGIN_PX +
            (args.px_per_inch > 0.0 ? px_from_sp(run.lines->overhang, args.px_per_inch) : 0.0);
        const double clip_top = static_cast<double>(cull_clip->y) - reach;
        const double clip_bottom = static_cast<double>(cull_clip->w) + reach;
        const auto top_of = [&](const LineSpan& s) {
            return origin_y + px_from_sp(s.top, args.px_per_inch);
        };
        const auto bottom_of = [&](const LineSpan& s) {
            return origin_y + px_from_sp(s.bottom, args.px_per_inch);
        };
        const auto place_span = [&](const LineSpan& s) {
            const size_t end = s.end < run.count ? s.end : run.count;
            const size_t first = s.first < end ? s.first : end;
            place_commands(run.cmds + first, end - first, run.ink, args, binder, stats);
        };
        size_t at = 0;
        if (run.lines->sorted && args.px_per_inch > 0.0) {
            if (!spans.empty() && !spans[0].bounded) {
                place_span(spans[0]);
                at = 1;
            }
            // Bottoms never decrease, so the first span reaching the top of
            // the clip rectangle is found by bisection, and tops never
            // decrease, so the walk ends at the first span below it.
            size_t low = at;
            size_t high = spans.size();
            while (low < high) {
                const size_t middle = low + (high - low) / 2;
                if (bottom_of(spans[middle]) < clip_top)
                    low = middle + 1;
                else
                    high = middle;
            }
            for (at = low; at < spans.size() && !(top_of(spans[at]) > clip_bottom); ++at)
                place_span(spans[at]);
        } else {
            for (const LineSpan& s : spans) {
                if (!s.bounded || !(bottom_of(s) < clip_top || top_of(s) > clip_bottom))
                    place_span(s);
            }
        }
    }
    return stats;
}

// --- Emission -------------------------------------------------------------------------

// The ops as quads. A TEXT op is what ImDrawList::AddText and
// ImFont::RenderText make of one character, test for test and in their order;
// a RECT_FILLED op is ImDrawList::AddRectFilled with square corners.
template<class Sink>
void Drawer::emit(Sink& sink, const ImVec4& clip, const DrawRules& rules)
{
    const std::vector<ImGuiBinder::Box>& boxes = _binder->boxes;
    const size_t count = _ops.size();
    size_t i = 0;
    while (i < count) {
        // Everything that can fail has been done by now. Between a
        // reservation and its end nothing is asked of the fonts and nothing
        // of ImGui but PrimRect, which checks nothing: there is no way out of
        // this loop, under a host whose IM_ASSERT throws either, that leaves
        // vertices reserved and not written.
        size_t end = i;
        size_t quads = 0;
        while (end < count && quads + MOST_QUADS_PER_OP <= BATCH_QUADS) {
            quads += _ops[end].type == OpType::RECT ? MOST_QUADS_PER_OP : 1;
            ++end;
        }
        typename Sink::Position at = sink.begin(quads);
        for (; i < end; ++i) {
            const DrawOp& op = _ops[i];
            if ((op.colour & IM_COL32_A_MASK) == 0)
                continue;
            // One test for a glyph, which nearly every op is.
            if (op.type != OpType::TEXT) {
                if (op.type == OpType::RECT_FILLED)
                    sink.rect(at, op.x0, op.y0, op.x1, op.y1, op.colour);
                else if (op.type == OpType::RECT)
                    write_frame(sink, at, op);
                else
                    write_segment(sink, at, op);
                continue;
            }
            const ImGuiBinder::Box& box = boxes[op.glyph];
            if (!box.visible || ImGuiBinder::outside_vertically(op.y0, box.size, clip))
                continue;
            const float x0 = op.x0 + box.x0;
            const float x1 = op.x0 + box.x1;
            if (!(x0 <= clip.z && x1 >= clip.x))
                continue;
            // ImFont::RenderText's test above keeps a glyph whose pen is ON
            // the clip rectangle's bottom edge, though a clip rectangle ends
            // before its bottom row and the whole quad is then under it; and
            // one whose cell reaches the top edge though its bitmap does not.
            if (rules.quads_off_clip_dropped &&
                !(op.y0 + box.y0 < clip.w && op.y0 + box.y1 > clip.y))
                continue;
            // A coloured glyph keeps its own colours and takes only the alpha.
            const ImU32 colour = box.coloured ? (op.colour | ~IM_COL32_A_MASK) : op.colour;
            at.quad(x0, op.y0 + box.y0, x1, op.y0 + box.y1, box.u0, box.v0, box.u1, box.v1, colour);
        }
        sink.end(at);
    }
}

DrawStats Drawer::draw_runs(ImDrawList* draw_list, const DrawRun* runs, size_t run_count,
                            const DrawArgs& args, const FaceFonts& fonts, MetricsResolver metrics)
{
    if (!IMTYPESET_CONTRACT(draw_list != nullptr, "draw_runs: no draw list"))
        return DrawStats();
    const ImVec4 clip = draw_list->_CmdHeader.ClipRect;
    _binder->configure(fonts, metrics, args.px_per_inch);
    const DrawStats stats = place(runs, run_count, args, *_binder, &clip);
    _binder->load(*this, clip);
    ListSink sink(draw_list);
    emit(sink, clip, args.rules);
    return stats;
}

DrawStats Drawer::draw_runs(DrawBuffers& out, const ImVec4& clip, const DrawRun* runs,
                            size_t run_count, const DrawArgs& args, const FaceFonts& fonts,
                            MetricsResolver metrics)
{
    _binder->configure(fonts, metrics, args.px_per_inch);
    const DrawStats stats = place(runs, run_count, args, *_binder, &clip);
    _binder->load(*this, clip);
    BufferSink sink(out);
    emit(sink, clip, args.rules);
    return stats;
}

} // namespace ImTypeset::detail
