// src/draw against the reference's ImDrawList backend, replaying
// tests/golden/draw (tools/oracle/FORMAT-draw.md):
//
//   - the pure helpers, exactly (tables.golden);
//   - the pen-level layer, exactly: the calls the reference made on the draw
//     list, with ImGui's baked size and ascent per key injected from the golden
//     so that no rasteriser is involved;
//   - the vertex layer, in a real headless frame through the port's own font
//     path: every quad must be the pen plus this build's own glyph box (so the
//     placement arithmetic is the reference's whatever the rasteriser), and
//     where this build's boxes and baked metrics equal the golden's the vertex,
//     index and command buffers must equal the reference's outright. The
//     census of differing boxes is printed.

#include "core_golden.h"
#include "support/imgui_compat.h"

#include "draw.h"
#include "font_loader.h"
#include "fontlib.h"
#include "style.h"

#include "imgui.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

// --- Fonts and frames -----------------------------------------------------------------

std::string font_path(const std::string& name)
{
    return golden::data_path("fonts/" + name);
}

// The reference's default registry: the files imgui-bundle ships, by role.
void register_defaults(FontLibrary& library)
{
    static const struct {
        Face face;
        const char* file;
    } defaults[] = {
        {Face::PROSE, "Roboto-Regular.ttf"},
        {Face::PROSE_BOLD, "Roboto-Bold.ttf"},
        {Face::PROSE_ITALIC, "Roboto-RegularItalic.ttf"},
        {Face::PROSE_BOLD_ITALIC, "Roboto-BoldItalic.ttf"},
        {Face::CODE, "Inconsolata-Medium.ttf"},
        {Face::MATH, "latinmodern-math.otf"},
    };
    for (const auto& entry : defaults) {
        std::string why;
        if (!library.register_font(entry.face, font_path(entry.file).c_str(), &why))
            FAIL("%s: %s", entry.file, why.c_str());
    }
}

// A headless ImGui context with the reference's display, for the length of a
// scope, and the faces loaded into its atlas as the reference loads them: one
// font per file at a registration size of 16, the bold code role sharing the
// bold prose font.
struct Stage {
    Stage()
    {
        register_defaults(library);
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        for (int i = 0; i < FACE_COUNT; ++i) {
            const Face face = static_cast<Face>(i);
            const Face backing = library.backing_face(face);
            if (backing != face)
                continue;
            const FaceData* data = library.face_data(face);
            std::string why;
            ImFont* font = data != nullptr
                               ? add_face_to_atlas(io.Fonts, *data, 16.0f, nullptr, &why)
                               : nullptr;
            if (font == nullptr)
                FAIL("%s: %s", face_value(face), why.c_str());
            loaded.faces[i].font = font;
            loaded.faces[i].data = data;
        }
        for (int i = 0; i < FACE_COUNT; ++i) {
            const Face backing = library.backing_face(static_cast<Face>(i));
            loaded.faces[i] = loaded.faces[static_cast<int>(backing)];
        }
        // The math file as shipped, through ImGui's own loading: its cmap has
        // none of the minted codepoints.
        unpatched_math = loaded;
        unpatched_math.faces[static_cast<int>(Face::MATH)].font =
            io.Fonts->AddFontFromFileTTF(font_path("latinmodern-math.otf").c_str(), 16.0f);
    }
    ~Stage() { ImGui::DestroyContext(); }
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    bool freetype() const
    {
        const char* name = ImGui::GetIO().Fonts->FontLoaderName;
        return name != nullptr && std::strncmp(name, "FreeType", 8) == 0;
    }

    FontLibrary library;
    FaceFonts loaded;
    FaceFonts unpatched_math;
    FaceFonts none;
};

struct Frame {
    Frame() { ImGui::NewFrame(); }
    ~Frame() { ImGui::Render(); }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
};

// --- A draw case as the golden holds it -----------------------------------------------

struct KeyRecord {
    FontKey key;
    std::string text;
    bool bound = false;
    float baked_size = 0.0f;
    float baked_ascent = 0.0f;
};

struct BoxRecord {
    FontKey key;
    uint32_t codepoint = 0;
    bool in_font = false;
    bool visible = false;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};

struct OpRecord {
    OpType type = OpType::TEXT;
    FontKey key;
    uint32_t codepoint = 0;
    uint32_t index = 0;
    float size = 0.0f;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    ImU32 colour = 0;
    int first_vertex = 0;
    int appended = 0;
};

struct VertexRecord {
    float x = 0.0f, y = 0.0f;
    ImU32 colour = 0;
};

struct CommandRecord {
    unsigned elem_count = 0, idx_offset = 0, vtx_offset = 0;
    ImVec4 clip;
};

struct DrawCase {
    std::string name;
    std::string fonts;
    DrawArgs args;
    ImVec4 clip;
    std::vector<KeyRecord> keys;
    std::vector<BoxRecord> boxes;
    std::vector<Fill> fills;
    std::vector<Rect> highlights;
    std::vector<InkRange> inks;
    std::vector<std::vector<DrawCmd>> commands;
    std::vector<ImU32> run_inks;
    std::vector<DrawRun> runs;
    DrawStats stats;
    std::vector<OpRecord> ops;
    std::vector<VertexRecord> vertices;
    std::vector<unsigned> indices;
    std::vector<CommandRecord> draw_commands;

    // Pointers into this object: call once it is where it will stay.
    void bind()
    {
        args.fills = fills.data();
        args.fill_count = fills.size();
        args.highlights = highlights.data();
        args.highlight_count = highlights.size();
        args.inks = inks.data();
        args.ink_count = inks.size();
        runs.clear();
        for (size_t i = 0; i < commands.size(); ++i) {
            DrawRun run;
            run.cmds = commands[i].data();
            run.count = commands[i].size();
            run.ink = run_inks[i];
            runs.push_back(run);
        }
    }

    const KeyRecord* key(FontKey k) const
    {
        for (const KeyRecord& record : keys) {
            if (record.key == k)
                return &record;
        }
        return nullptr;
    }

    const BoxRecord* box(FontKey k, uint32_t codepoint) const
    {
        for (const BoxRecord& record : boxes) {
            if (record.key == k && record.codepoint == codepoint)
                return &record;
        }
        return nullptr;
    }
};

float f32(golden::Cursor& c)
{
    return static_cast<float>(c.f64());
}

DrawCmd command_of(const core_golden::Cmd& cmd)
{
    const uint32_t index = static_cast<uint32_t>(cmd.index);
    switch (cmd.kind) {
    case core_golden::Cmd::GLYPH:
        return DrawCmd::glyph(cmd.font, cmd.codepoint, cmd.x, cmd.y, index);
    case core_golden::Cmd::RULERECT:
        return DrawCmd::rule_rect(cmd.x, cmd.y, cmd.width, cmd.height);
    case core_golden::Cmd::SETGLUE:
        return DrawCmd::set_glue(cmd.x, cmd.y, cmd.width, cmd.natural, cmd.height, cmd.depth,
                                 index);
    case core_golden::Cmd::ATOM:
        return DrawCmd::atom(cmd.x, cmd.y, cmd.width, cmd.natural, cmd.height, cmd.depth, index);
    case core_golden::Cmd::LINE:
        return DrawCmd::line(cmd.x, cmd.y, cmd.width, cmd.height, cmd.depth,
                             static_cast<uint32_t>(cmd.start), static_cast<uint32_t>(cmd.end));
    }
    return DrawCmd();
}

void read_case(const golden::File& file, const core_golden::Case& c, DrawCase& out)
{
    core_golden::Reader reader(file, c);
    core_golden::Context context;
    out.name = c.name;
    reader.take("note");
    out.fonts = std::string(reader.take("fonts").raw(0));
    {
        golden::Cursor origin(reader.take("origin"));
        out.args.origin.x = f32(origin);
        out.args.origin.y = f32(origin);
    }
    out.args.px_per_inch = reader.take("ppi").f64(0);
    out.args.debug = reader.take("debug").boolean(0);
    {
        golden::Cursor clip(reader.take("clip"));
        out.clip.x = f32(clip);
        out.clip.y = f32(clip);
        out.clip.z = f32(clip);
        out.clip.w = f32(clip);
    }
    reader.take("listflags");
    {
        golden::Cursor palette(reader.take("palette"));
        out.args.palette.highlight = palette.u32();
        out.args.palette.rule = palette.u32();
        out.args.palette.debug_box = palette.u32();
        out.args.palette.debug_baseline = palette.u32();
        out.args.palette.debug_stretched = palette.u32();
        out.args.palette.debug_shrunk = palette.u32();
        palette.expect_done();
    }
    for (uint32_t n = reader.take("keys").u32(0); n > 0; --n) {
        golden::Cursor k(reader.take("key"));
        KeyRecord record;
        record.text = k.str();
        record.key = context.faces.key(record.text);
        k.raw(); // role
        k.i64(); // size
        record.bound = k.boolean();
        k.f64(); // the metrics' ascent, descent and em, and the size requested:
        k.f64(); // the port derives all four itself
        k.f64();
        k.f64();
        record.baked_size = f32(k);
        record.baked_ascent = f32(k);
        f32(k); // baked descent
        k.expect_done();
        out.keys.push_back(record);
    }
    for (uint32_t n = reader.take("boxes").u32(0); n > 0; --n) {
        golden::Cursor b(reader.take("box"));
        BoxRecord record;
        record.key = context.faces.key(b.str());
        record.codepoint = b.u32();
        record.in_font = b.boolean();
        record.visible = b.boolean();
        // No glyph of the bundled fonts is coloured, so that path has no golden.
        CHECK(!b.boolean());
        record.x0 = f32(b);
        record.y0 = f32(b);
        record.x1 = f32(b);
        record.y1 = f32(b);
        f32(b); // advance: never read by the draw loop
        b.expect_done();
        out.boxes.push_back(record);
    }
    for (uint32_t n = reader.take("fills").u32(0); n > 0; --n) {
        golden::Cursor f(reader.take("fill"));
        Fill fill;
        fill.rect.x = f.i64();
        fill.rect.y = f.i64();
        fill.rect.width = f.i64();
        fill.rect.height = f.i64();
        fill.colour = f.u32();
        out.fills.push_back(fill);
    }
    for (uint32_t n = reader.take("highlights").u32(0); n > 0; --n) {
        golden::Cursor h(reader.take("highlight"));
        Rect rect;
        rect.x = h.i64();
        rect.y = h.i64();
        rect.width = h.i64();
        rect.height = h.i64();
        out.highlights.push_back(rect);
    }
    for (uint32_t n = reader.take("inks").u32(0); n > 0; --n) {
        golden::Cursor i(reader.take("ink"));
        InkRange ink;
        ink.start = i.u32();
        ink.end = i.u32();
        ink.colour = i.u32();
        out.inks.push_back(ink);
    }
    for (uint32_t n = reader.take("runs").u32(0); n > 0; --n) {
        out.run_inks.push_back(reader.take("run").u32(0));
        std::vector<DrawCmd> commands;
        for (const core_golden::Cmd& cmd : core_golden::read_cmds(reader, context))
            commands.push_back(command_of(cmd));
        out.commands.push_back(std::move(commands));
    }
    {
        golden::Cursor s(reader.take("stats"));
        out.stats.glyphs = s.i32();
        out.stats.missing = s.i32();
        out.stats.rules = s.i32();
    }
    for (uint32_t n = reader.take("ops").u32(0); n > 0; --n) {
        const golden::Record& record = reader.take();
        golden::Cursor o(record);
        OpRecord op;
        if (record.is("text")) {
            op.type = OpType::TEXT;
            op.key = context.faces.key(o.str());
            op.codepoint = o.u32();
            op.index = o.u32();
            op.size = f32(o);
            op.x0 = f32(o);
            op.y0 = f32(o);
            op.colour = o.u32();
        } else {
            if (record.is("rectfilled"))
                op.type = OpType::RECT_FILLED;
            else if (record.is("rect"))
                op.type = OpType::RECT;
            else if (record.is("linesegment"))
                op.type = OpType::LINE_SEGMENT;
            else
                FAIL("line %d: not an op record", record.line);
            op.x0 = f32(o);
            op.y0 = f32(o);
            op.x1 = f32(o);
            op.y1 = f32(o);
            op.colour = o.u32();
            if (op.type == OpType::LINE_SEGMENT)
                CHECK_EQ(o.f64(), 1.0); // the thickness the port passes too
        }
        op.first_vertex = o.i32();
        op.appended = o.i32();
        o.expect_done();
        out.ops.push_back(op);
    }
    for (uint32_t n = reader.take("vtx").u32(0); n > 0; --n) {
        golden::Cursor v(reader.take("v"));
        VertexRecord vertex;
        vertex.x = f32(v);
        vertex.y = f32(v);
        vertex.colour = v.u32();
        out.vertices.push_back(vertex);
    }
    for (uint32_t n = reader.take("tris").u32(0); n > 0; --n) {
        golden::Cursor t(reader.take("tri"));
        for (int i = 0; i < 3; ++i)
            out.indices.push_back(t.u32());
    }
    for (uint32_t n = reader.take("drawcmds").u32(0); n > 0; --n) {
        golden::Cursor d(reader.take("drawcmd"));
        CommandRecord command;
        command.elem_count = d.u32();
        command.idx_offset = d.u32();
        command.vtx_offset = d.u32();
        command.clip.x = f32(d);
        command.clip.y = f32(d);
        command.clip.z = f32(d);
        command.clip.w = f32(d);
        out.draw_commands.push_back(command);
    }
}

// Every case of draw.golden, each bound and ready to draw.
std::vector<DrawCase>& draw_cases()
{
    static std::vector<DrawCase> cases;
    if (!cases.empty())
        return cases;
    static golden::File file;
    if (!file.load(golden::path("draw/draw.golden")))
        return cases;
    const std::vector<core_golden::Case> listed = core_golden::cases(file, "draw/draw.golden");
    cases.resize(listed.size());
    for (size_t i = 0; i < listed.size(); ++i) {
        CHECK_CONTEXT("case %s (line %d)", listed[i].name.c_str(), listed[i].line);
        CHECK_EQ(listed[i].kind, std::string("draw"));
        read_case(file, listed[i], cases[i]);
        cases[i].bind();
    }
    return cases;
}

const DrawCase* find_case(const char* name)
{
    for (const DrawCase& c : draw_cases()) {
        if (c.name == name)
            return &c;
    }
    FAIL("no case '%s' in draw.golden", name);
    return nullptr;
}

// ImGui's answers as the golden recorded them: no rasteriser involved.
class GoldenBinder : public FontBinder
{
public:
    explicit GoldenBinder(const DrawCase& c)
        : _case(c)
    {
    }
    void begin() override { _keys.clear(); }
    Binding bind(uint32_t key_slot, FontKey key) override
    {
        CHECK_EQ(key_slot, static_cast<uint32_t>(_keys.size())); // numbered in first-use order
        const KeyRecord* record = _case.key(key);
        _keys.push_back(record);
        if (record == nullptr) {
            FAIL("the golden has no key record for face %u size %lld", key.face(),
                 static_cast<long long>(key.size()));
            return Binding::UNKNOWN;
        }
        return record->bound ? Binding::BOUND : Binding::UNBOUND;
    }
    bool has_glyph(uint32_t glyph_slot, uint32_t key_slot, uint32_t codepoint) override
    {
        CHECK_EQ(glyph_slot, _asked++);
        const KeyRecord* key = _keys[key_slot];
        if (!key->bound)
            return true;
        const BoxRecord* box = _case.box(key->key, codepoint);
        if (box == nullptr) {
            FAIL("the golden has no box record for %s U+%04X", key->text.c_str(), codepoint);
            return false;
        }
        return box->in_font;
    }
    bool bake(uint32_t key_slot, FontKey, float* size, float* ascent) override
    {
        ++bakes;
        *size = _keys[key_slot]->baked_size;
        *ascent = _keys[key_slot]->baked_ascent;
        return true;
    }

    int bakes = 0;

private:
    const DrawCase& _case;
    std::vector<const KeyRecord*> _keys;
    uint32_t _asked = 0;
};

bool same_bits(float a, float b)
{
    return std::memcmp(&a, &b, sizeof a) == 0 || a == b;
}

// The port's ops against the golden's: the pen-level layer, exactly. For a
// glyph drawn in ImGui's current font the golden wrote a size of 0, the size
// being ImGui's to choose.
void check_ops(const DrawCase& c, const Drawer& drawer)
{
    const std::vector<DrawOp>& ops = drawer.ops();
    CHECK_EQ(ops.size(), c.ops.size());
    const size_t count = ops.size() < c.ops.size() ? ops.size() : c.ops.size();
    int wrong = 0;
    for (size_t i = 0; i < count && wrong < 5; ++i) {
        const DrawOp& got = ops[i];
        const OpRecord& want = c.ops[i];
        bool same = got.type == want.type && got.colour == want.colour &&
                    same_bits(got.x0, want.x0) && same_bits(got.y0, want.y0);
        if (same && got.type == OpType::TEXT) {
            const Drawer::Glyph& glyph = drawer.glyphs()[got.glyph];
            const Drawer::Key& key = drawer.keys()[glyph.key];
            const float size = key.binding == FontBinder::Binding::BOUND ? key.size : 0.0f;
            same = key.key == want.key && glyph.codepoint == want.codepoint &&
                   got.index == want.index && same_bits(size, want.size);
        } else if (same) {
            same = same_bits(got.x1, want.x1) && same_bits(got.y1, want.y1);
        }
        if (!same) {
            ++wrong;
            FAIL("op %zu differs: port type %d at (%a, %a)-(%a, %a) colour %u; reference type %d "
                 "at (%a, %a)-(%a, %a) colour %u",
                 i, static_cast<int>(got.type), static_cast<double>(got.x0),
                 static_cast<double>(got.y0), static_cast<double>(got.x1),
                 static_cast<double>(got.y1), got.colour, static_cast<int>(want.type),
                 static_cast<double>(want.x0), static_cast<double>(want.y0),
                 static_cast<double>(want.x1), static_cast<double>(want.y1), want.colour);
        }
    }
}

void check_stats(const DrawStats& got, const DrawStats& want)
{
    CHECK_EQ(got.glyphs, want.glyphs);
    CHECK_EQ(got.missing, want.missing);
    CHECK_EQ(got.rules, want.rules);
}

const FaceFonts& fonts_of(const Stage& stage, const DrawCase& c)
{
    if (c.fonts == "none")
        return stage.none;
    if (c.fonts == "unpatched-math")
        return stage.unpatched_math;
    CHECK_EQ(c.fonts, std::string("loaded"));
    return stage.loaded;
}

// A draw list with a clip rectangle current, as the oracle set its background
// list up for a case. Pops what it pushed.
struct Canvas {
    Canvas(ImDrawList* draw_list, const ImVec4& clip)
        : list(draw_list)
    {
        const ImVec4 display = list->_CmdHeader.ClipRect;
        pushed = std::memcmp(&display, &clip, sizeof display) != 0;
        if (pushed)
            list->PushClipRect(ImVec2(clip.x, clip.y), ImVec2(clip.z, clip.w), false);
        const ImVec4 now = list->_CmdHeader.ClipRect;
        CHECK(std::memcmp(&now, &clip, sizeof now) == 0);
    }
    explicit Canvas(const DrawCase& c)
        : Canvas(ImGui::GetBackgroundDrawList(), c.clip)
    {
    }
    ~Canvas()
    {
        if (pushed)
            list->PopClipRect();
    }
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    ImDrawList* list;
    bool pushed = false;
};

std::string utf8_of(uint32_t codepoint)
{
    std::string out;
    if (codepoint == 0)
        return out; // what a NUL makes of a C string
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return out;
}

// Makes on `list` the calls the reference makes for the ops a draw placed:
// one ImDrawList::AddText per glyph, with ImDrawListFlags_TextNoPixelSnap set
// as the reference sets it, and ImGui's own rectangles. What ImGui then holds
// is what the port's direct writes must equal, whatever the rasteriser is.
void replay_through_imgui(ImDrawList* list, const Drawer& drawer, const FaceFonts& fonts)
{
    const imgui_compat::DrawListFlags flags = list->Flags;
    list->Flags |= imgui_compat::TextNoPixelSnap;
    // One rectangle as ImDrawList::AddRectFilled writes one with square
    // corners in v1.92.9, whatever its corners are: later versions drop one
    // that is empty or inside out, and anti-alias its edges.
    const auto filled = [list](const ImVec2& a, const ImVec2& b, ImU32 colour) {
        if ((colour & IM_COL32_A_MASK) == 0)
            return;
        list->PrimReserve(6, 4);
        list->PrimRect(a, b, colour);
    };
    for (const DrawOp& op : drawer.ops()) {
        const ImVec2 a(op.x0, op.y0);
        const ImVec2 b(op.x1, op.y1);
        switch (op.type) {
        case OpType::TEXT: {
            const Drawer::Glyph& glyph = drawer.glyphs()[op.glyph];
            const Drawer::Key& key = drawer.keys()[glyph.key];
            FontSpec spec;
            if (!parse_font_key(key.key, &spec)) {
                FAIL("an op was placed for a key that names no face");
                break;
            }
            const FaceFont& face = fonts.faces[static_cast<int>(spec.face)];
            const uint32_t asked = face.data != nullptr && glyph.codepoint != 0
                                       ? atlas_codepoint(*face.data, glyph.codepoint)
                                       : glyph.codepoint;
            const std::string text = utf8_of(asked);
            const char* begin = text.c_str();
            if (face.font != nullptr)
                list->AddText(face.font, key.size, a, op.colour, begin, begin + text.size());
            else
                list->AddText(a, op.colour, begin, begin + text.size());
            break;
        }
        case OpType::RECT_FILLED:
            filled(a, b, op.colour);
            break;
        case OpType::RECT:
            // The overlay's line box: not ImGui's AddRect, but the frame of
            // the rectangle one pixel thick, as src/draw.h defines it.
            if (!(b.x > a.x && b.y > a.y))
                break;
            if (!(b.x - a.x > 2.0f && b.y - a.y > 2.0f)) {
                filled(a, b, op.colour);
                break;
            }
            filled(a, ImVec2(b.x, a.y + 1.0f), op.colour);
            filled(ImVec2(a.x, b.y - 1.0f), b, op.colour);
            filled(ImVec2(a.x, a.y + 1.0f), ImVec2(a.x + 1.0f, b.y - 1.0f), op.colour);
            filled(ImVec2(b.x - 1.0f, a.y + 1.0f), ImVec2(b.x, b.y - 1.0f), op.colour);
            break;
        case OpType::LINE_SEGMENT:
            // Its baseline: the row of pixels below y, not ImGui's AddLine.
            if (b.x > a.x)
                filled(a, ImVec2(b.x, a.y + 1.0f), op.colour);
            break;
        }
    }
    list->Flags = flags;
}

// Two draw lists holding the same thing, byte for byte: vertices (position,
// texture coordinate, colour), indices, and the commands that group them.
void check_same_lists(const ImDrawList& got, const ImDrawList& want)
{
    REQUIRE_EQ(got.VtxBuffer.Size, want.VtxBuffer.Size);
    REQUIRE_EQ(got.IdxBuffer.Size, want.IdxBuffer.Size);
    for (int i = 0; i < got.VtxBuffer.Size; ++i) {
        const ImDrawVert& g = got.VtxBuffer[i];
        const ImDrawVert& w = want.VtxBuffer[i];
        if (std::memcmp(&g, &w, sizeof g) != 0) {
            FAIL("vertex %d: port (%g, %g) uv (%g, %g) colour %u; ImGui (%g, %g) uv (%g, %g) "
                 "colour %u",
                 i, static_cast<double>(g.pos.x), static_cast<double>(g.pos.y),
                 static_cast<double>(g.uv.x), static_cast<double>(g.uv.y), g.col,
                 static_cast<double>(w.pos.x), static_cast<double>(w.pos.y),
                 static_cast<double>(w.uv.x), static_cast<double>(w.uv.y), w.col);
            return;
        }
    }
    CHECK(got.IdxBuffer.Size == 0 ||
          std::memcmp(got.IdxBuffer.Data, want.IdxBuffer.Data,
                      static_cast<size_t>(got.IdxBuffer.Size) * sizeof(ImDrawIdx)) == 0);
    REQUIRE_EQ(got.CmdBuffer.Size, want.CmdBuffer.Size);
    for (int i = 0; i < got.CmdBuffer.Size; ++i) {
        CHECK_EQ(got.CmdBuffer[i].ElemCount, want.CmdBuffer[i].ElemCount);
        CHECK_EQ(got.CmdBuffer[i].IdxOffset, want.CmdBuffer[i].IdxOffset);
        CHECK_EQ(got.CmdBuffer[i].VtxOffset, want.CmdBuffer[i].VtxOffset);
        CHECK(std::memcmp(&got.CmdBuffer[i].ClipRect, &want.CmdBuffer[i].ClipRect,
                          sizeof(ImVec4)) == 0);
    }
}

// Buffers holding what a draw list holds: the same vertices, and indices that
// name the same vertex once the list's per-command vertex offset is added.
void check_buffers_hold(const DrawBuffers& buffers, const ImDrawList& list)
{
    REQUIRE_EQ(buffers.vertices.Size, list.VtxBuffer.Size);
    REQUIRE_EQ(buffers.indices.Size, list.IdxBuffer.Size);
    CHECK(list.VtxBuffer.Size == 0 ||
          std::memcmp(list.VtxBuffer.Data, buffers.vertices.Data,
                      static_cast<size_t>(buffers.vertices.Size) * sizeof(ImDrawVert)) == 0);
    int checked = 0;
    for (const ImDrawCmd& command : list.CmdBuffer) {
        for (unsigned i = 0; i < command.ElemCount; ++i) {
            const int at = static_cast<int>(command.IdxOffset + i);
            if (static_cast<unsigned>(list.IdxBuffer[at]) + command.VtxOffset !=
                buffers.indices[at]) {
                FAIL("index %d names another vertex", at);
                return;
            }
            ++checked;
        }
    }
    CHECK_EQ(checked, buffers.indices.Size);
}

// How this build's rasteriser differs from the one the golden was taken
// under, over the keys and boxes of one case.
struct Census {
    int keys = 0;
    int keys_differing = 0;
    int boxes = 0;
    int boxes_differing = 0;
};

Census census_of(const DrawCase& c, const FaceFonts& fonts, const Drawer& drawer)
{
    Census census;
    for (const Drawer::Key& key : drawer.keys()) {
        const KeyRecord* record = c.key(key.key);
        if (record == nullptr || !record->bound || !key.asked)
            continue;
        ++census.keys;
        if (!key.drawable || key.size != record->baked_size || key.ascent != record->baked_ascent)
            ++census.keys_differing;
    }
    for (const Drawer::Glyph& glyph : drawer.glyphs()) {
        const Drawer::Key& key = drawer.keys()[glyph.key];
        const BoxRecord* record = c.box(key.key, glyph.codepoint);
        FontSpec spec;
        if (record == nullptr || !parse_font_key(key.key, &spec))
            continue;
        const FaceFont& face = fonts.faces[static_cast<int>(spec.face)];
        if (face.font == nullptr)
            continue;
        ++census.boxes;
        if (glyph.present != record->in_font) {
            ++census.boxes_differing;
            continue;
        }
        if (!glyph.present || !key.drawable)
            continue;
        const uint32_t asked =
            face.data != nullptr ? atlas_codepoint(*face.data, glyph.codepoint) : glyph.codepoint;
        // May load a glyph the draw dropped before looking at it; the caller
        // has finished with the atlas by now.
        const ImFontGlyph* box =
            face.font->GetFontBaked(key.size)->FindGlyph(static_cast<ImWchar>(asked));
        if ((box->Visible != 0) != record->visible || box->X0 != record->x0 ||
            box->Y0 != record->y0 || box->X1 != record->x1 || box->Y1 != record->y1)
            ++census.boxes_differing;
    }
    return census;
}

} // namespace

// --- The pure helpers -----------------------------------------------------------------

TEST(the_pure_helpers_match_the_reference)
{
    golden::File file;
    REQUIRE(file.load(golden::path("draw/tables.golden")));
    int snaps = 0, spans = 0, sizes = 0, ats = 0, colours = 0, members = 0;
    for (const golden::Record& r : file) {
        CHECK_CONTEXT("line %d", r.line);
        golden::Cursor c(r);
        if (r.is("snap_px")) {
            const double value = c.f64();
            CHECK_EQ(snap_px(value), c.f64());
            ++snaps;
        } else if (r.is("snap_span")) {
            const double from = c.f64();
            const double to = c.f64();
            double lo = 0.0, hi = 0.0;
            snap_span(from, to, &lo, &hi);
            CHECK_EQ(lo, c.f64());
            CHECK_EQ(hi, c.f64());
            ++spans;
        } else if (r.is("baked_size_px")) {
            const double size = c.f64();
            const double ascent = c.f64();
            const double descent = c.f64();
            const double em = c.f64();
            CHECK_EQ(baked_size_px(size, ascent, descent, em), c.f64());
            ++sizes;
        } else if (r.is("at")) {
            const float origin = f32(c);
            const Scaled x = c.i64();
            const double ppi = c.f64();
            CHECK_EQ(static_cast<double>(device_coordinate(origin, x, ppi)), c.f64());
            ++ats;
        } else if (r.is("rgba")) {
            const unsigned red = c.u32(), green = c.u32(), blue = c.u32(), alpha = c.u32();
            CHECK_EQ(rgba(red, green, blue, alpha), c.u32());
            ++colours;
        } else if (r.is("palette")) {
            const std::string which(c.raw());
            const std::string member(c.raw());
            const Palette palette = which == "dark" ? dark_palette() : default_palette();
            CHECK(which == "dark" || which == "default");
            static const struct {
                const char* name;
                ImU32 Palette::*field;
            } fields[] = {
                {"page", &Palette::page},
                {"ink", &Palette::ink},
                {"rule", &Palette::rule},
                {"highlight", &Palette::highlight},
                {"debug_box", &Palette::debug_box},
                {"debug_baseline", &Palette::debug_baseline},
                {"debug_stretched", &Palette::debug_stretched},
                {"debug_shrunk", &Palette::debug_shrunk},
                {"link", &Palette::link},
            };
            bool known = false;
            for (const auto& field : fields) {
                if (member == field.name) {
                    CHECK_EQ(palette.*field.field, c.u32());
                    known = true;
                }
            }
            CHECK(known);
            ++members;
        } else {
            continue;
        }
        c.expect_done();
    }
    CHECK_EQ(snaps, 38);
    CHECK_EQ(spans, 20);
    CHECK_EQ(sizes, 55);
    CHECK_EQ(ats, 108);
    CHECK_EQ(colours, 5);
    CHECK_EQ(members, 18);
    CHECK_EQ(rgba(1, 2, 3), IM_COL32(1, 2, 3, 255));
}

TEST(snap_px_is_not_im_round)
{
    // floor(v + 0.5), in double: a negative half goes up, and the largest
    // double below a half goes to 1 because the sum rounds.
    CHECK_EQ(snap_px(-0.5), 0.0);
    CHECK_EQ(snap_px(-0.75), -1.0);
    CHECK_EQ(snap_px(-1.5), -1.0);
    CHECK_EQ(snap_px(2.5), 3.0);
    CHECK_EQ(snap_px(0.49999999999999994), 1.0);
    // ImGui's IM_ROUND truncates: it answers 0 for -0.75.
    CHECK_EQ(static_cast<double>(static_cast<float>(static_cast<int>(-0.75f + 0.5f))), 0.0);
    // It is floor(v + 0.5) for every double, the ones no pixel grid reaches
    // included: around each power of two up to 2^70, at the halves either
    // side of zero, and over a few hundred thousand values in between.
    uint64_t state = 0x9E3779B97F4A7C15ull;
    int compared = 0;
    for (int exponent = -30; exponent <= 70; ++exponent) {
        const double base = std::ldexp(1.0, exponent);
        for (int step = -4096; step <= 4096; ++step) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            const double jitter = static_cast<double>(state >> 11) / 9007199254740992.0;
            for (const double value :
                 {base + step * 0.5, base * (1.0 + jitter), std::nextafter(base + step * 0.5, 0.0),
                  step + jitter, step * 0.5 + base * jitter}) {
                for (const double signed_value : {value, -value}) {
                    const double got = snap_px(signed_value);
                    const double want = std::floor(signed_value + 0.5);
                    if (!(got == want) || std::signbit(got) != std::signbit(want)) {
                        FAIL("snap_px(%a) is %a; floor(v + 0.5) is %a", signed_value, got, want);
                        return;
                    }
                    ++compared;
                }
            }
        }
    }
    CHECK(compared > 8000000);
    CHECK_EQ(snap_px(std::numeric_limits<double>::infinity()),
             std::numeric_limits<double>::infinity());
    CHECK_EQ(snap_px(-std::numeric_limits<double>::infinity()),
             -std::numeric_limits<double>::infinity());
    CHECK(std::isnan(snap_px(std::numeric_limits<double>::quiet_NaN())));
    CHECK_EQ(snap_px(1.0e300), 1.0e300);
    CHECK(!std::signbit(snap_px(-0.5)));
    CHECK(std::signbit(snap_px(-0.75)));

    // A hairline keeps a pixel; nothing stays nothing; a reversed span is empty.
    double lo = 0.0, hi = 0.0;
    snap_span(10.6, 10.9, &lo, &hi);
    CHECK_EQ(lo, 11.0);
    CHECK_EQ(hi, 12.0);
    snap_span(10.6, 10.6, &lo, &hi);
    CHECK_EQ(lo, hi);
    snap_span(10.9, 10.6, &lo, &hi);
    CHECK_EQ(lo, hi);
}

// --- The pen-level layer ----------------------------------------------------------------

TEST(placement_makes_the_calls_the_reference_made)
{
    const std::vector<DrawCase>& cases = draw_cases();
    REQUIRE_EQ(cases.size(), static_cast<size_t>(22));
    Drawer drawer;
    size_t ops = 0;
    for (const DrawCase& c : cases) {
        CHECK_CONTEXT("case %s", c.name.c_str());
        GoldenBinder binder(c);
        const DrawStats stats = drawer.place(c.runs.data(), c.runs.size(), c.args, binder, nullptr);
        check_stats(stats, c.stats);
        check_ops(c, drawer);
        ops += drawer.ops().size();
        // One key record per key, one box record per (key, codepoint), each in
        // first-use order: the port met the same ones, and asked once.
        CHECK_EQ(drawer.keys().size(), c.keys.size());
        CHECK_EQ(drawer.glyphs().size(), c.boxes.size());
        for (size_t i = 0; i < drawer.keys().size() && i < c.keys.size(); ++i)
            CHECK(drawer.keys()[i].key == c.keys[i].key);
        for (size_t i = 0; i < drawer.glyphs().size() && i < c.boxes.size(); ++i) {
            CHECK_EQ(drawer.glyphs()[i].codepoint, c.boxes[i].codepoint);
            CHECK(drawer.keys()[drawer.glyphs()[i].key].key == c.boxes[i].key);
        }
        CHECK(binder.bakes <= static_cast<int>(c.keys.size()));
    }
    CHECK(ops > 3000);
}

namespace
{

// Round numbers: the prose face bakes at 20 px with an ascent of 12.5, the
// code face at 10 px with 7.25, and neither has an 'M'. No real face has a
// fractional baked ascent, which is why the anchor's rule needs a fake to be
// told from its neighbours.
class RoundBinder : public FontBinder
{
public:
    void begin() override {}
    Binding bind(uint32_t, FontKey key) override
    {
        _faces.push_back(key.face());
        return key.face() < static_cast<uint32_t>(FACE_COUNT) ? Binding::BOUND : Binding::UNKNOWN;
    }
    bool has_glyph(uint32_t, uint32_t, uint32_t codepoint) override { return codepoint != 'M'; }
    bool bake(uint32_t key_slot, FontKey, float* size, float* ascent) override
    {
        const bool code = _faces[key_slot] == static_cast<uint32_t>(Face::CODE);
        *size = code ? 10.0f : 20.0f;
        *ascent = code ? 7.25f : 12.5f;
        return true;
    }

private:
    std::vector<uint32_t> _faces;
};

Scaled sp_of(double pixels)
{
    return static_cast<Scaled>(std::llround(pixels * 65536.0));
}

} // namespace

TEST(a_span_is_anchored_once_and_its_gaps_are_rounded)
{
    // At 72.27 px per inch a scaled point is 1/65536 px exactly, so the
    // arithmetic below can be followed by hand.
    const FontKey prose = font_key(Face::PROSE, 20 * UNITY);
    const FontKey code = font_key(Face::CODE, 10 * UNITY);
    const Scaled y = sp_of(20.2);
    const std::vector<DrawCmd> commands = {
        DrawCmd::glyph(prose, 'a', sp_of(0.4), y, 0),
        DrawCmd::glyph(prose, 'M', sp_of(1.0), y, 1), // missing: the span carries on across it
        DrawCmd::glyph(prose, 'c', sp_of(1.8), y, 2),
        DrawCmd::glyph(prose, 'd', sp_of(3.3), y, 3),
        DrawCmd::glyph(code, 'e', sp_of(4.3), y, 4),            // a face change inside the span
        DrawCmd::glyph(prose, 'f', sp_of(5.3), sp_of(18.7), 5), // raised by a pixel and a half
        DrawCmd::set_glue(sp_of(6.0), y, sp_of(0.6), sp_of(0.6), 0, 0, 6),
        DrawCmd::glyph(prose, 'g', sp_of(6.6), y, 7),
        DrawCmd::line(0, y, sp_of(100.0), sp_of(10.0), sp_of(3.0), 8, 10),
        DrawCmd::glyph(prose, 'h', sp_of(8.3), y, 8),
        DrawCmd::rule_rect(sp_of(9.0), y, sp_of(0.5), sp_of(1.0)),
        DrawCmd::glyph(prose, 'i', sp_of(9.6), y, 9),
    };
    // The gaps the test depends on are exact in scaled points.
    REQUIRE_EQ(commands[3].x - commands[2].x, 3 * UNITY / 2);
    REQUIRE_EQ(commands[5].y - y, -3 * UNITY / 2);
    DrawRun run;
    run.cmds = commands.data();
    run.count = commands.size();
    run.ink = 0xFF000000u;
    DrawArgs args;
    args.px_per_inch = PT_PER_INCH;
    Drawer drawer;
    RoundBinder binder;
    DrawStats stats = drawer.place(&run, 1, args, binder);
    CHECK_EQ(stats.glyphs, 8);
    CHECK_EQ(stats.missing, 1);
    CHECK_EQ(stats.rules, 1);
    struct Pen {
        float x, y;
    };
    const Pen expected[] = {
        // The anchor: x snapped, and the top of the cell is snap(20.2 - 12.5)
        // = 8 — the baseline less the ascent, snapped once, and not the
        // baseline snapped and the ascent then taken off, which is 7.5.
        {0.0f, 8.0f},
        // 1.4 px on from 'a': a gap of 1. Placed from its own position it
        // would be at snap(1.8) = 2.
        {1.0f, 8.0f},
        // A gap of exactly 1.5 rounds up, to 2.
        {3.0f, 8.0f},
        // The code face's cell hangs from an ascent 5.25 px shorter: 5 rows down.
        {4.0f, 13.0f},
        // Raised 1.5 px: snap(-1.5) is -1, a half going up the number line.
        {5.0f, 7.0f},
        // After the glue, a new anchor at snap(6.6).
        {7.0f, 8.0f},
        // A line ends the span too: snap(8.3) = 8, where a gap of 1.7 on from
        // 'g' would have made it 9.
        {8.0f, 8.0f},
        // And a rule: snap(9.6) = 10, where a gap of 1.3 would have made it 9.
        {10.0f, 8.0f},
    };
    std::vector<DrawOp> pens;
    for (const DrawOp& op : drawer.ops()) {
        if (op.type == OpType::TEXT)
            pens.push_back(op);
    }
    CHECK_EQ(drawer.ops().size(), pens.size() + 1); // and the rule
    REQUIRE_EQ(pens.size(), static_cast<size_t>(IM_ARRAYSIZE(expected)));
    for (size_t i = 0; i < pens.size(); ++i) {
        CHECK_CONTEXT("glyph %zu", i);
        CHECK_EQ(pens[i].x0, expected[i].x);
        CHECK_EQ(pens[i].y0, expected[i].y);
    }

    // Scrolled to a negative, fractional position the anchor moves and the
    // span keeps its shape: every pen is the same whole number of pixels from
    // the first. snap(-10.1) is -10; a truncating round would say -9.
    args.origin = ImVec2(-10.5f, -3.25f);
    stats = drawer.place(&run, 1, args, binder);
    REQUIRE(drawer.ops().size() > 5);
    CHECK_EQ(drawer.ops()[0].x0, -10.0f);
    CHECK_EQ(drawer.ops()[0].y0, 4.0f); // snap(20.2 - 3.25 - 12.5) = snap(4.45)
    for (size_t i = 1; i < 5; ++i) {
        CHECK_CONTEXT("glyph %zu", i);
        CHECK_EQ(drawer.ops()[i].x0 - drawer.ops()[0].x0, expected[i].x - expected[0].x);
        CHECK_EQ(drawer.ops()[i].y0 - drawer.ops()[0].y0, expected[i].y - expected[0].y);
    }
}

// --- The vertex layer -------------------------------------------------------------------

TEST(the_vertices_are_those_of_one_add_text_per_glyph)
{
    Stage stage;
    const std::vector<DrawCase>& cases = draw_cases();
    REQUIRE(!cases.empty());
    const MetricsResolver metrics = fontlib_metrics(stage.library);
    Drawer drawer;
    Drawer buffered;
    DrawBuffers buffers;
    Census total;
    int outright = 0;
    int debug_at_pen_level = 0;
    for (const DrawCase& c : cases) {
        CHECK_CONTEXT("case %s", c.name.c_str());
        const FaceFonts& fonts = fonts_of(stage, c);
        Frame frame;
        Canvas canvas(c);
        REQUIRE_EQ(canvas.list->VtxBuffer.Size, 0);
        const imgui_compat::DrawListFlags flags = canvas.list->Flags;
        const DrawStats stats =
            drawer.draw_runs(canvas.list, c.runs.data(), c.runs.size(), c.args, fonts, metrics);
        CHECK_EQ(canvas.list->Flags, flags);
        const ImDrawList& list = *canvas.list;

        // Rule 1, under any rasteriser: what the port wrote directly is what
        // ImGui writes when it is handed the same calls one glyph at a time,
        // as the reference hands them — unbound faces included, and the debug
        // overlay with its outlines as this library draws them. So a quad
        // differs from the reference's by the difference of two glyph boxes
        // and by nothing else.
        {
            Canvas imgui(ImGui::GetForegroundDrawList(), c.clip);
            replay_through_imgui(imgui.list, drawer, fonts);
            check_same_lists(list, *imgui.list);
        }

        // The same quads through the buffer output, the overlay included.
        {
            buffers.clear();
            const DrawStats again = buffered.draw_runs(buffers, c.clip, c.runs.data(),
                                                       c.runs.size(), c.args, fonts, metrics);
            check_stats(again, stats);
            check_buffers_hold(buffers, list);
        }

        const Census census = census_of(c, fonts, drawer);
        total.keys += census.keys;
        total.keys_differing += census.keys_differing;
        total.boxes += census.boxes;
        total.boxes_differing += census.boxes_differing;
        if (census.keys_differing != 0 || census.boxes_differing != 0)
            continue;

        // Rule 2, where this build's rasteriser answered as the reference's
        // did: the same calls, and then the same buffers, outright.
        ++outright;
        check_stats(stats, c.stats);
        check_ops(c, drawer);
        if (c.fonts == "none")
            continue; // drawn in ImGui's current font, which is this context's own
        if (c.args.debug) {
            // The reference's buffers hold what ImGui's AddRect and AddLine
            // made of the overlay's outlines; the port draws those itself
            // (docs/divergences.md). The calls were compared above.
            ++debug_at_pen_level;
            continue;
        }
        REQUIRE_EQ(static_cast<size_t>(list.VtxBuffer.Size), c.vertices.size());
        REQUIRE_EQ(static_cast<size_t>(list.IdxBuffer.Size), c.indices.size());
        int wrong = 0;
        for (size_t i = 0; i < c.vertices.size() && wrong < 5; ++i) {
            const ImDrawVert& got = list.VtxBuffer[static_cast<int>(i)];
            const VertexRecord& want = c.vertices[i];
            if (!same_bits(got.pos.x, want.x) || !same_bits(got.pos.y, want.y) ||
                got.col != want.colour) {
                ++wrong;
                FAIL("vertex %zu: port (%a, %a) colour %u, reference (%a, %a) colour %u", i,
                     static_cast<double>(got.pos.x), static_cast<double>(got.pos.y), got.col,
                     static_cast<double>(want.x), static_cast<double>(want.y), want.colour);
            }
        }
        for (size_t i = 0; i < c.indices.size() && wrong < 5; ++i) {
            if (list.IdxBuffer[static_cast<int>(i)] != c.indices[i]) {
                ++wrong;
                FAIL("index %zu: port %u, reference %u", i,
                     static_cast<unsigned>(list.IdxBuffer[static_cast<int>(i)]), c.indices[i]);
            }
        }
        // The draw commands that hold them: the same element counts, offsets
        // and clip rectangles.
        REQUIRE_EQ(static_cast<size_t>(list.CmdBuffer.Size), c.draw_commands.size());
        for (size_t i = 0; i < c.draw_commands.size(); ++i) {
            const ImDrawCmd& command = list.CmdBuffer[static_cast<int>(i)];
            CHECK_EQ(command.ElemCount, c.draw_commands[i].elem_count);
            CHECK_EQ(command.IdxOffset, c.draw_commands[i].idx_offset);
            CHECK_EQ(command.VtxOffset, c.draw_commands[i].vtx_offset);
            CHECK(std::memcmp(&command.ClipRect, &c.draw_commands[i].clip, sizeof(ImVec4)) == 0);
        }
    }
    std::printf("draw: %d of %d glyph boxes and %d of %d baked keys differ from the reference's "
                "(FreeType 2.13.3); %d of %zu cases compared with its buffers outright; this "
                "build's loader is %s, ImWchar is %d bits\n",
                total.boxes_differing, total.boxes, total.keys_differing, total.keys, outright,
                cases.size(), ImGui::GetIO().Fonts->FontLoaderName, ATLAS_WCHAR_BITS);
    CHECK(total.boxes > 400);
    // Which codepoints a face has and what size ImGui rounds a request to are
    // not the rasteriser's business: the size and the stats hold under any.
    CHECK_EQ(total.keys_differing, 0);
    // Under FreeType the glyph boxes are the reference's, so rule 2 is not a
    // matter of luck: every case was compared with the reference's buffers.
    // Under stb_truetype the boxes differ and rule 1 is what was held.
    const char* const loader = ImGui::GetIO().Fonts->FontLoaderName;
    if (loader != nullptr && std::strncmp(loader, "FreeType", 8) == 0) {
        CHECK_EQ(total.boxes_differing, 0);
        CHECK_EQ(static_cast<size_t>(outright), cases.size());
        // Of those, the cases with the debug overlay stop at the calls.
        std::printf("draw: %d of them with the debug overlay, compared at the calls and not at "
                    "the vertices\n",
                    debug_at_pen_level);
        CHECK_EQ(debug_at_pen_level, 2);
    } else {
        std::printf("draw: the comparison with the reference's buffers is not gated under %s\n",
                    loader != nullptr ? loader : "this loader");
    }
}

TEST(a_clip_rectangle_drops_the_quads_imgui_drops_at_every_edge)
{
    // ImGui's tests on a glyph against the clip rectangle are a mix of strict
    // and non-strict comparisons, in float. One line and one paragraph, with
    // the rectangle's edges swept in half pixels across pens, cell bottoms and
    // glyph boxes so that every comparison meets its equal case.
    Stage stage;
    const MetricsResolver metrics = fontlib_metrics(stage.library);
    Drawer drawer;
    int compared = 0;
    for (const char* name : {"single-line", "span-edges-by-hand", "runs-highlights-fills-inks"}) {
        const DrawCase* c = find_case(name);
        REQUIRE(c != nullptr);
        CHECK_CONTEXT("case %s", name);
        // Where the first glyph's pen is, from an unclipped placement.
        float pen_x = 0.0f, pen_y = 0.0f;
        {
            Frame frame;
            drawer.draw_runs(ImGui::GetBackgroundDrawList(), c->runs.data(), c->runs.size(),
                             c->args, stage.loaded, metrics);
            for (const DrawOp& op : drawer.ops()) {
                if (op.type == OpType::TEXT) {
                    pen_x = op.x0;
                    pen_y = op.y0;
                    break;
                }
            }
        }
        std::vector<ImVec4> clips;
        for (float d = -4.0f; d <= 44.0f; d += 0.5f) {
            clips.push_back(ImVec4(0.0f, pen_y + d, 800.0f, 600.0f));        // top edge
            clips.push_back(ImVec4(0.0f, 0.0f, 800.0f, pen_y + d));          // bottom edge
            clips.push_back(ImVec4(pen_x + d - 3.0f, 0.0f, 800.0f, 600.0f)); // left edge
            clips.push_back(ImVec4(0.0f, 0.0f, pen_x + d - 3.0f, 600.0f));   // right edge
        }
        for (const ImVec4& clip : clips) {
            CHECK_CONTEXT("clip (%g, %g)-(%g, %g)", static_cast<double>(clip.x),
                          static_cast<double>(clip.y), static_cast<double>(clip.z),
                          static_cast<double>(clip.w));
            Frame frame;
            Canvas canvas(ImGui::GetBackgroundDrawList(), clip);
            drawer.draw_runs(canvas.list, c->runs.data(), c->runs.size(), c->args, stage.loaded,
                             metrics);
            Canvas imgui(ImGui::GetForegroundDrawList(), clip);
            replay_through_imgui(imgui.list, drawer, stage.loaded);
            check_same_lists(*canvas.list, *imgui.list);
            ++compared;
        }
    }
    CHECK_EQ(compared, 3 * 4 * 97);
}

TEST(the_stats_and_missing_glyphs_do_not_depend_on_the_rasteriser)
{
    Stage stage;
    const MetricsResolver metrics = fontlib_metrics(stage.library);
    Drawer drawer;
    for (const DrawCase& c : draw_cases()) {
        CHECK_CONTEXT("case %s", c.name.c_str());
        Frame frame;
        Canvas canvas(c);
        const DrawStats stats = drawer.draw_runs(canvas.list, c.runs.data(), c.runs.size(), c.args,
                                                 fonts_of(stage, c), metrics);
        check_stats(stats, c.stats);
        // The pens too: they are built from the baked ascent, which is the
        // font's own number rounded, and from nothing the rasteriser drew.
        check_ops(c, drawer);
    }
}

// --- Culling by line ------------------------------------------------------------------

TEST(a_line_index_partitions_its_commands)
{
    for (const DrawCase& c : draw_cases()) {
        CHECK_CONTEXT("case %s", c.name.c_str());
        for (const std::vector<DrawCmd>& commands : c.commands) {
            LineIndex index;
            index_lines(commands.data(), commands.size(), index);
            size_t next = 0;
            for (const LineSpan& span : index.spans) {
                CHECK_EQ(static_cast<size_t>(span.first), next);
                CHECK(span.end > span.first);
                next = span.end;
                // Every span but a leading one starts with the line that covers it.
                if (span.first != 0)
                    CHECK(commands[span.first].is(CmdType::LINE));
                if (span.bounded) {
                    CHECK(span.top <= span.bottom);
                    for (uint32_t i = span.first; i < span.end; ++i) {
                        const DrawCmd& cmd = commands[i];
                        if (cmd.is(CmdType::LINE)) {
                            CHECK(cmd.y - cmd.height >= span.top);
                            CHECK(cmd.y + cmd.depth <= span.bottom);
                        }
                    }
                }
            }
            CHECK_EQ(next, commands.size());
        }
    }
    LineIndex index;
    index.spans.push_back(LineSpan());
    index_lines(nullptr, 0, index);
    CHECK(index.spans.empty());
    CHECK(index.sorted);
}

TEST(a_line_nested_in_a_line_stays_in_its_span)
{
    const FontKey key = font_key(Face::PROSE, 10 * UNITY);
    // A line holding a fraction: its numerator and denominator are lines of a
    // box inside it, and the glyph after them is the outer line's.
    const std::vector<DrawCmd> commands = {
        DrawCmd::rule_rect(0, -50, 100, 10),      // before any line
        DrawCmd::line(0, 100, 500, 40, 30, 0, 4), // the outer line
        DrawCmd::glyph(key, 'a', 0, 100, 0),
        DrawCmd::line(20, 80, 30, 10, 2, 1, 2), // numerator, inside it
        DrawCmd::glyph(key, '1', 20, 80, 1),
        DrawCmd::line(20, 120, 30, 10, 60, 2, 3), // denominator, hanging low
        DrawCmd::glyph(key, '2', 20, 120, 2),
        DrawCmd::rule_rect(20, 95, 30, 2),
        DrawCmd::glyph(key, 'b', 60, 100, 3),
        DrawCmd::line(0, 200, 500, 40, 30, 4, 4), // an empty line
        DrawCmd::line(0, 300, 500, 40, 30, 4, 5),
        DrawCmd::glyph(key, 'c', 0, 300, 4),
    };
    LineIndex index;
    index_lines(commands.data(), commands.size(), index);
    REQUIRE_EQ(index.spans.size(), static_cast<size_t>(4));
    CHECK_EQ(index.spans[0].first, 0u);
    CHECK_EQ(index.spans[0].end, 1u);
    CHECK(index.spans[0].bounded);
    CHECK_EQ(index.spans[0].top, -50);
    CHECK_EQ(index.spans[0].bottom, -40);
    CHECK_EQ(index.spans[1].first, 1u);
    CHECK_EQ(index.spans[1].end, 9u);
    CHECK_EQ(index.spans[1].top, 60);
    CHECK_EQ(index.spans[1].bottom, 180); // the denominator's depth, not the outer line's
    CHECK_EQ(index.spans[2].end, 10u);
    CHECK_EQ(index.spans[3].end, 12u);
    CHECK(index.sorted);

    // A glyph no line covers makes its span one that is always drawn, and a
    // list that goes back up the page is not sorted.
    const std::vector<DrawCmd> loose = {
        DrawCmd::glyph(key, 'a', 0, 100, 0), DrawCmd::line(0, 300, 500, 40, 30, 1, 2),
        DrawCmd::glyph(key, 'b', 0, 300, 1), DrawCmd::line(0, 200, 500, 40, 30, 2, 3),
        DrawCmd::glyph(key, 'c', 0, 200, 2),
    };
    index_lines(loose.data(), loose.size(), index);
    REQUIRE_EQ(index.spans.size(), static_cast<size_t>(3));
    CHECK(!index.spans[0].bounded);
    CHECK(index.spans[1].bounded);
    CHECK(!index.sorted);
}

namespace
{

// Draws one case with and without culling by line and holds the culled draw to
// its promise: the quads it writes are quads of the full draw, in order, and
// every quad it leaves out lies wholly outside the clip rectangle.
void check_culling(Stage& stage, const DrawCase& c, const ImVec4& clip, bool shuffled,
                   size_t* full_ops, size_t* culled_ops)
{
    const MetricsResolver metrics = fontlib_metrics(stage.library);
    const FaceFonts& fonts = fonts_of(stage, c);
    std::vector<std::vector<DrawCmd>> commands = c.commands;
    std::vector<LineIndex> indices(commands.size());
    std::vector<DrawRun> runs = c.runs;
    for (size_t i = 0; i < commands.size(); ++i) {
        index_lines(commands[i].data(), commands[i].size(), indices[i]);
        // The same spans in an order that is not down the page: the visible
        // ones must then be found by testing each.
        if (shuffled) {
            CHECK(indices[i].sorted);
            indices[i].sorted = false;
        }
        runs[i].cmds = commands[i].data();
        runs[i].lines = &indices[i];
    }
    Frame frame;
    Drawer drawer;
    DrawBuffers full;
    DrawBuffers culled;
    DrawArgs args = c.args;
    drawer.draw_runs(full, clip, runs.data(), runs.size(), args, fonts, metrics);
    *full_ops = drawer.ops().size();
    args.cull_lines = true;
    drawer.draw_runs(culled, clip, runs.data(), runs.size(), args, fonts, metrics);
    *culled_ops = drawer.ops().size();

    REQUIRE(culled.vertices.Size <= full.vertices.Size);
    int at = 0;
    int dropped = 0;
    for (int q = 0; q * 4 < full.vertices.Size; ++q) {
        const ImDrawVert* quad = full.vertices.Data + q * 4;
        if (at * 4 < culled.vertices.Size &&
            std::memcmp(quad, culled.vertices.Data + at * 4, 4 * sizeof(ImDrawVert)) == 0) {
            ++at;
            continue;
        }
        ++dropped;
        // Left out: it must not reach into the clip rectangle.
        const bool outside = quad[2].pos.y <= clip.y || quad[0].pos.y >= clip.w ||
                             quad[2].pos.x <= clip.x || quad[0].pos.x >= clip.z;
        if (!outside) {
            FAIL("culling dropped a quad at (%g, %g)-(%g, %g) that the clip rectangle shows",
                 static_cast<double>(quad[0].pos.x), static_cast<double>(quad[0].pos.y),
                 static_cast<double>(quad[2].pos.x), static_cast<double>(quad[2].pos.y));
        }
    }
    CHECK_EQ(at * 4, culled.vertices.Size);
    (void)dropped;
}

} // namespace

TEST(culling_by_line_drops_only_what_the_clip_rectangle_hides)
{
    Stage stage;
    size_t full = 0, culled = 0;
    for (const DrawCase& c : draw_cases()) {
        if (c.fonts != "loaded")
            continue;
        CHECK_CONTEXT("case %s", c.name.c_str());
        // The case's own clip rectangle, then bands cutting through it: one
        // line's worth, a hairline, one above everything and one below.
        check_culling(stage, c, c.clip, false, &full, &culled);
        CHECK(culled <= full);
        const float y = c.args.origin.y;
        const ImVec4 bands[] = {
            ImVec4(0.0f, y + 30.0f, 800.0f, y + 52.0f),
            ImVec4(0.0f, y + 41.5f, 800.0f, y + 41.75f),
            ImVec4(0.0f, -5000.0f, 800.0f, -4000.0f),
            ImVec4(0.0f, 5000.0f, 800.0f, 6000.0f),
            ImVec4(120.0f, y + 10.0f, 180.0f, y + 90.0f),
        };
        for (const ImVec4& band : bands) {
            CHECK_CONTEXT("band %g..%g", static_cast<double>(band.y), static_cast<double>(band.w));
            check_culling(stage, c, band, false, &full, &culled);
            check_culling(stage, c, band, true, &full, &culled);
        }
    }

    // And it is proportional to what is visible: the paragraph the reference
    // cuts on four sides keeps under a third of its calls, and one band of a
    // long paragraph a few lines of it.
    const DrawCase* cut = find_case("clip-rect-cutting-lines");
    REQUIRE(cut != nullptr);
    check_culling(stage, *cut, cut->clip, false, &full, &culled);
    CHECK_EQ(full, static_cast<size_t>(254));
    CHECK(culled > 0 && culled * 3 < full);
    const DrawCase* paragraph = find_case("justified-paragraph");
    REQUIRE(paragraph != nullptr);
    check_culling(stage, *paragraph, ImVec4(0.0f, 5000.0f, 800.0f, 6000.0f), false, &full, &culled);
    CHECK_EQ(culled, static_cast<size_t>(0));
}

// --- Reuse, and input nobody shipped --------------------------------------------------

TEST(a_drawer_reused_draws_the_same_and_allocates_nothing_more)
{
    Stage stage;
    const MetricsResolver metrics = fontlib_metrics(stage.library);
    const DrawCase* c = find_case("mixed-faces-and-sizes");
    REQUIRE(c != nullptr);
    Drawer drawer;
    DrawBuffers first;
    DrawBuffers second;
    {
        Frame frame;
        drawer.draw_runs(first, c->clip, c->runs.data(), c->runs.size(), c->args, stage.loaded,
                         metrics);
    }
    const size_t ops_capacity = drawer.ops().capacity();
    const size_t keys_capacity = drawer.keys().capacity();
    const size_t glyphs_capacity = drawer.glyphs().capacity();
    for (int frame_number = 0; frame_number < 3; ++frame_number) {
        Frame frame;
        second.clear();
        drawer.draw_runs(second, c->clip, c->runs.data(), c->runs.size(), c->args, stage.loaded,
                         metrics);
        REQUIRE_EQ(second.vertices.Size, first.vertices.Size);
        // Positions and colours; the texture coordinates are the atlas's to
        // move between frames.
        for (int i = 0; i < first.vertices.Size; ++i) {
            if (first.vertices[i].pos.x != second.vertices[i].pos.x ||
                first.vertices[i].pos.y != second.vertices[i].pos.y ||
                first.vertices[i].col != second.vertices[i].col) {
                FAIL("vertex %d moved between two draws of the same commands", i);
                break;
            }
        }
    }
    CHECK(first.vertices.Size > 0);
    CHECK_EQ(drawer.ops().capacity(), ops_capacity);
    CHECK_EQ(drawer.keys().capacity(), keys_capacity);
    CHECK_EQ(drawer.glyphs().capacity(), glyphs_capacity);
    // Appending: a second draw into the same buffers numbers its indices on.
    const int vertices = second.vertices.Size;
    const int indices = second.indices.Size;
    {
        Frame frame;
        drawer.draw_runs(second, c->clip, c->runs.data(), c->runs.size(), c->args, stage.loaded,
                         metrics);
    }
    REQUIRE_EQ(second.vertices.Size, vertices * 2);
    REQUIRE_EQ(second.indices.Size, indices * 2);
    CHECK_EQ(second.indices[indices], static_cast<unsigned>(vertices));
}

TEST(more_quads_than_one_reservation_holds)
{
    // Enough glyphs that the draw list is filled in several reservations and,
    // with 16-bit indices, across more than one draw command.
    Stage stage;
    const MetricsResolver metrics = fontlib_metrics(stage.library);
    const FontKey key = font_key(Face::PROSE, 789381);
    std::vector<DrawCmd> commands;
    const int rows = 40, columns = 500;
    for (int row = 0; row < rows; ++row) {
        const Scaled y = (row + 1) * 10 * UNITY;
        commands.push_back(DrawCmd::line(0, y, 600 * UNITY, 8 * UNITY, 2 * UNITY,
                                         static_cast<uint32_t>(row * columns),
                                         static_cast<uint32_t>((row + 1) * columns)));
        for (int column = 0; column < columns; ++column) {
            commands.push_back(DrawCmd::glyph(key, static_cast<uint32_t>('a' + column % 26),
                                              column * UNITY, y,
                                              static_cast<uint32_t>(row * columns + column)));
        }
    }
    DrawRun run;
    run.cmds = commands.data();
    run.count = commands.size();
    run.ink = rgba(1, 2, 3);
    DrawArgs args;
    args.origin = ImVec2(10.0f, 10.0f);
    Drawer drawer;
    DrawBuffers buffers;
    // What a renderer that can draw more than 64k vertices a list announces.
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    Frame frame;
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    REQUIRE((list->Flags & imgui_compat::AllowVtxOffset) != 0);
    const DrawStats stats = drawer.draw_runs(list, &run, 1, args, stage.loaded, metrics);
    CHECK_EQ(stats.glyphs, rows * columns);
    drawer.draw_runs(buffers, list->_CmdHeader.ClipRect, &run, 1, args, stage.loaded, metrics);
    CHECK_EQ(buffers.vertices.Size, rows * columns * 4);
    CHECK(sizeof(ImDrawIdx) != 2 || list->CmdBuffer.Size > 1);
    check_buffers_hold(buffers, *list);
}

TEST(commands_nobody_shipped_draw_nothing_and_break_nothing)
{
    Stage stage;
    const MetricsResolver metrics = fontlib_metrics(stage.library);
    const Scaled huge = std::numeric_limits<Scaled>::max();
    const Scaled tiny = std::numeric_limits<Scaled>::min();
    const FontKey prose = font_key(Face::PROSE, 789381);
    std::vector<DrawCmd> commands = {
        DrawCmd::line(tiny, huge, huge, huge, huge, 5, 2),
        // A key of all ones, first: it is not "no key yet".
        DrawCmd::glyph(FontKey{~uint64_t(0)}, 'a', 0, 0, 0),
        DrawCmd::glyph(FontKey{~uint64_t(0)}, 0x2014, 0, 0, 0),
        DrawCmd::glyph(FontKey::make(99, 789381), 'a', 0, 0, 0), // no such face
        DrawCmd::glyph(font_key(Face::PROSE, 0), 'a', 0, 0, 1),  // no size
        DrawCmd::glyph(font_key(Face::PROSE, 1), 'a', 0, 0, 2),  // under half a pixel
        DrawCmd::glyph(font_key(Face::PROSE, FontKey::MAX_SIZE), 'a', 0, 0, 3), // absurd
        DrawCmd::glyph(prose, 0x110000, 0, 0, 4),                               // not a codepoint
        DrawCmd::glyph(prose, 0xFFFFFFFFu, 0, 0, 5),
        DrawCmd::glyph(prose, 0xD800, 0, 0, 6), // a surrogate
        DrawCmd::glyph(prose, 'a', huge, tiny, 7),
        DrawCmd::glyph(prose, 'b', tiny, huge, 8),
        DrawCmd::rule_rect(tiny, tiny, huge, huge),
        DrawCmd::rule_rect(huge, huge, tiny, tiny),
        DrawCmd::set_glue(huge, tiny, huge, tiny, huge, huge, 9),
        DrawCmd::atom(tiny, tiny, tiny, huge, tiny, tiny, 10),
        DrawCmd::glyph(prose, 'c', 0, 0, 11),
    };
    DrawCmd corrupt = DrawCmd::glyph(prose, 'd', 0, 0, 12);
    std::memset(&corrupt.type, 0x7F, sizeof corrupt.type);
    commands.push_back(corrupt);

    const InkRange inks[] = {{9, 3, 1}, {0, 0xFFFFFFFFu, 2}, {5, 6, 3}, {5, 6, 4}};
    const Fill fills[] = {{Rect{tiny, tiny, huge, huge}, rgba(1, 1, 1)},
                          {Rect{huge, huge, huge, huge}, rgba(1, 1, 1)}};
    LineIndex index;
    index_lines(commands.data(), commands.size(), index);
    LineIndex foreign;
    foreign.spans.push_back(LineSpan{7, 9000, 0, 0, true});
    foreign.spans.push_back(LineSpan{9000, 3, 0, 0, false});

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const ImVec2 origins[] = {ImVec2(0.0f, 0.0f), ImVec2(nan, nan), ImVec2(inf, -inf),
                              ImVec2(-3.0e38f, 3.0e38f)};
    const double densities[] = {96.0, 0.0, -96.0, 1.0e300,
                                std::numeric_limits<double>::quiet_NaN()};
    Drawer drawer;
    DrawBuffers buffers;
    for (const ImVec2& origin : origins) {
        for (const double density : densities) {
            for (const LineIndex* lines :
                 {static_cast<const LineIndex*>(nullptr), static_cast<const LineIndex*>(&index),
                  static_cast<const LineIndex*>(&foreign)}) {
                Frame frame;
                DrawRun runs[3];
                runs[0].cmds = commands.data();
                runs[0].count = commands.size();
                runs[0].lines = lines;
                runs[1].cmds = nullptr;
                runs[1].count = 7;
                runs[2].cmds = commands.data();
                runs[2].count = 0;
                DrawArgs args;
                args.origin = origin;
                args.px_per_inch = density;
                args.inks = inks;
                args.ink_count = IM_ARRAYSIZE(inks);
                args.fills = fills;
                args.fill_count = IM_ARRAYSIZE(fills);
                args.debug = true;
                args.cull_lines = lines != nullptr;
                ImDrawList* list = ImGui::GetBackgroundDrawList();
                const DrawStats stats =
                    drawer.draw_runs(list, runs, 3, args, stage.loaded, metrics);
                if (lines == nullptr) {
                    // Not drawable: the face that is not one, the sizes ImGui
                    // cannot bake, the codepoints that are not codepoints.
                    CHECK(stats.missing >= 9);
                    CHECK_EQ(stats.rules, 2);
                }
                // Whatever was written is whole and refers to itself: every
                // index names a vertex that is there.
                const auto self_contained = [](const ImVector<ImDrawVert>& vertices,
                                               const auto& indices) {
                    for (const auto at : indices) {
                        if (static_cast<int64_t>(at) >= vertices.Size)
                            return false;
                    }
                    return true;
                };
                CHECK(self_contained(list->VtxBuffer, list->IdxBuffer));
                buffers.clear();
                drawer.draw_runs(buffers, ImVec4(nan, -inf, inf, nan), runs, 3, args, stage.none,
                                 MetricsResolver());
                CHECK(self_contained(buffers.vertices, buffers.indices));
            }
        }
    }
    // No runs, no draw list.
    {
        Frame frame;
        const DrawStats nothing = drawer.draw_runs(ImGui::GetBackgroundDrawList(), nullptr, 0,
                                                   DrawArgs(), stage.loaded, metrics);
        CHECK_EQ(nothing.glyphs + nothing.missing + nothing.rules, 0);
        static int violations = 0;
        violations = 0;
        const ContractHandler before = contract_handler();
        contract_handler() = [](const char*) { ++violations; };
        drawer.draw_runs(static_cast<ImDrawList*>(nullptr), nullptr, 0, DrawArgs(), stage.loaded,
                         metrics);
        contract_handler() = before;
        CHECK_EQ(violations, 1);
    }
}

TEST(an_unbound_face_is_drawn_in_the_current_font_and_a_keyless_metrics_at_nominal_size)
{
    Stage stage;
    const FontKey key = font_key(Face::PROSE, 789381);
    const std::vector<DrawCmd> commands = {
        DrawCmd::glyph(key, 'T', 0, 20 * UNITY, 0),
        DrawCmd::glyph(key, 'o', 9 * UNITY, 20 * UNITY, 1),
    };
    DrawRun run;
    run.cmds = commands.data();
    run.count = commands.size();
    run.ink = rgba(9, 9, 9);
    DrawArgs args;
    Drawer drawer;
    Frame frame;
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    // No face bound: two quads all the same, at ImGui's current size.
    DrawStats stats = drawer.draw_runs(list, &run, 1, args, stage.none, MetricsResolver());
    CHECK_EQ(stats.glyphs, 2);
    CHECK_EQ(stats.missing, 0);
    CHECK_EQ(list->VtxBuffer.Size, 8);
    REQUIRE_EQ(drawer.keys().size(), static_cast<size_t>(1));
    CHECK(drawer.keys()[0].binding == FontBinder::Binding::UNBOUND);
    CHECK_EQ(drawer.keys()[0].size, ImGui::GetFontSize());
    // Bound, but the resolver knows nothing: the key's own size in pixels,
    // 789381 sp at 96 px per inch, is 16 and is what ImGui is asked for.
    stats = drawer.draw_runs(list, &run, 1, args, stage.loaded, MetricsResolver());
    CHECK_EQ(stats.glyphs, 2);
    CHECK_EQ(drawer.keys()[0].size, 16.0f);
    // With the face's metrics the em is corrected to ImGui's pixel height.
    stats = drawer.draw_runs(list, &run, 1, args, stage.loaded, fontlib_metrics(stage.library));
    CHECK_EQ(drawer.keys()[0].size, 19.0f);
    CHECK_EQ(list->VtxBuffer.Size, 24);
}

// Review item D1 (.concerns/review-draw-para-style--reviewer--findings.md). A
// hand-built command may carry any key bits. All ones was place_commands' own
// "no key yet" value, so a first glyph with that key never got a key slot and
// the tables were read where nothing had been put (ASan: heap-buffer-overflow
// reading _latin, and _keys for a codepoint past 255).
TEST(a_first_glyph_whose_key_is_all_ones_gets_a_key_slot_like_any_other)
{
    Stage stage;
    const MetricsResolver metrics = fontlib_metrics(stage.library);
    const FontKey ones{~uint64_t(0)};
    for (const uint32_t codepoint : {uint32_t('a'), uint32_t(0x2014), uint32_t(0x10FFFF)}) {
        CHECK_CONTEXT("U+%04X", codepoint);
        const std::vector<DrawCmd> commands = {
            DrawCmd::glyph(ones, codepoint, 0, 20 * UNITY, 0),
            DrawCmd::glyph(ones, 'b', 9 * UNITY, 20 * UNITY, 1),
            DrawCmd::glyph(font_key(Face::PROSE, 789381), 'c', 18 * UNITY, 20 * UNITY, 2),
        };
        DrawRun run;
        run.cmds = commands.data();
        run.count = commands.size();
        Drawer drawer;
        Frame frame;
        const DrawStats stats = drawer.draw_runs(ImGui::GetBackgroundDrawList(), &run, 1,
                                                 DrawArgs(), stage.loaded, metrics);
        // The key names no face: its two glyphs are missing, the third drawn.
        CHECK_EQ(stats.missing, 2);
        CHECK_EQ(stats.glyphs, 1);
        REQUIRE_EQ(drawer.keys().size(), static_cast<size_t>(2));
        CHECK(drawer.keys()[0].binding == FontBinder::Binding::UNKNOWN);
        DrawBuffers buffers;
        const DrawStats again = drawer.draw_runs(buffers, ImVec4(0.0f, 0.0f, 800.0f, 600.0f), &run,
                                                 1, DrawArgs(), stage.none, MetricsResolver());
        CHECK_EQ(again.glyphs + again.missing, 3);
    }
}

// Review item D2. With no face bound a glyph is drawn in whatever font is
// current and nothing is asked of it first, as the reference asks nothing
// (imgui_draw.py, _Preambles.character): a codepoint the current font lacks is
// drawn as that font's fallback. The answer must not depend on which glyph of
// the key was met first, which under culling changes with the scroll.
TEST(an_unbound_face_draws_every_codepoint_whichever_comes_first)
{
    Stage stage;
    const FontKey key = font_key(Face::PROSE, 789381);
    // The current font has 'a' and has no U+4E00.
    const uint32_t absent = 0x4E00;
    for (int order = 0; order < 2; ++order) {
        CHECK_CONTEXT("order %d", order);
        const uint32_t first = order == 0 ? uint32_t('a') : absent;
        const uint32_t second = order == 0 ? absent : uint32_t('a');
        const std::vector<DrawCmd> commands = {
            DrawCmd::glyph(key, first, 0, 20 * UNITY, 0),
            DrawCmd::glyph(key, second, 9 * UNITY, 20 * UNITY, 1),
        };
        DrawRun run;
        run.cmds = commands.data();
        run.count = commands.size();
        run.ink = rgba(9, 9, 9);
        Drawer drawer;
        Frame frame;
        REQUIRE(ImGui::GetFont()->IsGlyphInFont(static_cast<ImWchar>('a')));
        REQUIRE(!ImGui::GetFont()->IsGlyphInFont(static_cast<ImWchar>(absent)));
        ImDrawList* const list = ImGui::GetBackgroundDrawList();
        const int vertices_before = list->VtxBuffer.Size;
        const DrawStats stats =
            drawer.draw_runs(list, &run, 1, DrawArgs(), stage.none, MetricsResolver());
        CHECK_EQ(stats.glyphs, 2);
        CHECK_EQ(stats.missing, 0);
        // Two quads either way: the absent one is the font's fallback glyph.
        CHECK_EQ(list->VtxBuffer.Size - vertices_before, 8);
    }
}
