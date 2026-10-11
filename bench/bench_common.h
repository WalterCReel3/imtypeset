// What the benchmarks share: the report header, the timer, the row format, the
// sample text and a headless ImGui with the fonts added.
//
// No third-party framework (docs/decisions.md D9). A row is one line,
//
//   | what | n | min | median | p95 | unit | note |
//
// so that a run pastes into docs/performance.md as it stands and
// bench/spread.py can line the rows of several runs up. The minimum is the
// closest thing to the cost of the work; the median and the 95th percentile
// say how far a frame strays from it on this machine.
//
// THE TEXT is bench/data/sample.txt, which tools/sample_text.py writes from
// the reference's demo/sample.md: its prose as plain paragraphs. The port has
// no markdown, so headings are paragraphs here and code blocks and tables are
// not in it.

#pragma once

#include "imtypeset.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace bench
{

// The reference's page: its litmus measure, 420 pt, at its 96 pixels per inch,
// with its 16 px em (the body of advtext.style.Theme.build()).
constexpr double MEASURE_PT = 420.0;
constexpr double PX_PER_INCH = 96.0;
constexpr float EM_PX = 16.0f;
// The reference's paragraph skip, 0.55 em, in pixels.
constexpr float PARAGRAPH_SKIP_PX = 8.8f;
constexpr float WINDOW_PX = 768.0f;

inline float px_of_pt(double pt)
{
    return static_cast<float>(pt * PX_PER_INCH / 72.27);
}

inline double now_us()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::micro>(clock::now().time_since_epoch()).count();
}

struct Samples {
    std::vector<double> us;

    void add(double value) { us.push_back(value); }
    size_t count() const { return us.size(); }
    // Sorts; call once every sample is in.
    double at(double fraction)
    {
        if (us.empty())
            return 0.0;
        std::sort(us.begin(), us.end());
        const size_t last = us.size() - 1;
        const size_t index = static_cast<size_t>(static_cast<double>(last) * fraction + 0.5);
        return us[std::min(index, last)];
    }
    double min() { return at(0.0); }
    double median() { return at(0.5); }
    double p95() { return at(0.95); }
    double max() { return at(1.0); }
};

// `body` run `warm` times untimed and then `count` times timed, one sample each.
template<class Body>
Samples sample(int count, int warm, Body&& body)
{
    Samples samples;
    for (int i = -warm; i < count; ++i) {
        const double start = now_us();
        body();
        const double stop = now_us();
        if (i >= 0)
            samples.add(stop - start);
    }
    return samples;
}

// As `sample`, with each run inside an ImGui frame of its own: a draw list
// keeps its vertices until Render, so several draws in one frame would time a
// buffer growing under the measurement. `body` receives the background list.
template<class Body>
Samples sample_frames(int count, int warm, Body&& body)
{
    Samples samples;
    for (int i = -warm; i < count; ++i) {
        ImGui::NewFrame();
        ImDrawList* list = ImGui::GetBackgroundDrawList();
        const double start = now_us();
        body(list);
        const double stop = now_us();
        ImGui::Render();
        if (i >= 0)
            samples.add(stop - start);
    }
    return samples;
}

inline void table_head(const char* title)
{
    std::printf("\n### %s\n\n| what | n | min | median | p95 | unit | note |\n"
                "|---|---|---|---|---|---|---|\n",
                title);
}

// Microseconds in, a unit chosen by the median out.
inline void row(const char* what, Samples samples, const char* note = "")
{
    const double median = samples.median();
    const char* unit = "us";
    double scale = 1.0;
    if (median >= 10000.0) {
        unit = "ms";
        scale = 1.0e-3;
    } else if (median < 1.0) {
        unit = "ns";
        scale = 1.0e3;
    }
    std::printf("| %s | %zu | %.3f | %.3f | %.3f | %s | %s |\n", what, samples.count(),
                samples.min() * scale, median * scale, samples.p95() * scale, unit, note);
    std::fflush(stdout);
}

inline std::string first_line_of(const char* path, const char* prefix)
{
    std::string found;
    if (std::FILE* file = std::fopen(path, "r")) {
        char line[512];
        const size_t size = std::strlen(prefix);
        while (std::fgets(line, sizeof line, file) != nullptr) {
            if (std::strncmp(line, prefix, size) != 0)
                continue;
            found = line + size;
            while (!found.empty() && (found.back() == '\n' || found.back() == ' '))
                found.pop_back();
            const size_t colon = found.find(':');
            if (colon != std::string::npos)
                found.erase(0, found.find_first_not_of(" \t", colon + 1));
            break;
        }
        std::fclose(file);
    }
    return found;
}

// Keeps this thread busy for IMTYPESET_BENCH_WARMUP_S seconds (8 when unset or
// unreadable) and returns the seconds spent. Every benchmark calls it, through
// header(), before its first timed row: on a processor that has been idle the
// first seconds of work run at a lower clock, and the rows printed first would
// be the ones to carry it (docs/performance.md, "A benchmark started cold").
// It times nothing and touches nothing the rows measure.
inline double warm_up()
{
    double seconds = 8.0;
    if (const char* text = std::getenv("IMTYPESET_BENCH_WARMUP_S")) {
        char* end = nullptr;
        const double asked = std::strtod(text, &end);
        if (end != text && asked >= 0.0 && asked <= 600.0)
            seconds = asked;
    }
    const double start = now_us();
    const double stop = start + seconds * 1.0e6;
    volatile double sink = 0.0;
    double x = 1.0;
    while (now_us() < stop) {
        for (int i = 0; i < 100000; ++i)
            x = x * 1.0000001 + 1.0e-9;
        if (x > 1.0e100)
            x = 1.0;
        sink = sink + x;
    }
    return (now_us() - start) * 1.0e-6;
}

// What a number means nothing without. The flags string is the global flags
// and the build type's; imtypeset_build_flags adds the warnings,
// -fno-exceptions -fno-rtti and -ffp-contract=off to every source of ours,
// which the string does not show (bench/CMakeLists.txt).
inline void header(const char* name)
{
    std::printf("## %s\n\n", name);
    std::printf("- compiler: %s, build type %s, flags \"%s\" plus -ffp-contract=off "
                "-fno-exceptions -fno-rtti\n",
                IMTYPESET_BENCH_COMPILER, IMTYPESET_BENCH_BUILD_TYPE, IMTYPESET_BENCH_CXX_FLAGS);
    const std::string cpu = first_line_of("/proc/cpuinfo", "model name");
    const std::string load = first_line_of("/proc/loadavg", "");
    std::printf("- machine: %s; load average %s\n", cpu.empty() ? "unknown" : cpu.c_str(),
                load.empty() ? "unknown" : load.c_str());
    std::printf("- imtypeset %s, Dear ImGui %s", ImTypeset::GetVersion(), IMGUI_VERSION);
#ifdef IMGUI_ENABLE_FREETYPE
    const char* rasteriser = "FreeType";
#else
    const char* rasteriser = "stb_truetype";
#endif
    // The atlas names its loader once it has built; before that, the build's.
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().Fonts->FontLoaderName != nullptr)
        rasteriser = ImGui::GetIO().Fonts->FontLoaderName;
    std::printf(", rasteriser %s", rasteriser);
    std::printf(", ImWchar %d bits, ImDrawIdx %d bits\n", static_cast<int>(sizeof(ImWchar) * 8),
                static_cast<int>(sizeof(ImDrawIdx) * 8));
    if (std::strcmp(IMTYPESET_BENCH_BUILD_TYPE, "Release") != 0)
        std::printf("- NOT A RELEASE BUILD: these numbers are not timings to quote\n");
    std::printf("- text: bench/data/sample.txt, the prose of the reference's demo/sample.md as "
                "plain paragraphs (the port has no markdown: headings are set as paragraphs, "
                "code blocks and tables are left out)\n");
    const double warmed = warm_up();
    std::printf("- warm-up: %.1f s of untimed arithmetic on this thread before the first row "
                "(IMTYPESET_BENCH_WARMUP_S sets the seconds, 0 for none)\n",
                warmed);
    std::fflush(stdout);
}

inline std::string font_path(const char* file)
{
    return std::string(IMTYPESET_TEST_DATA_DIR "/fonts/") + file;
}

struct FontFile {
    ImTypesetFace face;
    const char* file;
};

// The reference's bundle, role for role (docs/decisions.md D13).
inline const std::vector<FontFile>& font_files()
{
    static const std::vector<FontFile> files = {
        {ImTypesetFace_Regular, "Roboto-Regular.ttf"},
        {ImTypesetFace_Bold, "Roboto-Bold.ttf"},
        {ImTypesetFace_Italic, "Roboto-RegularItalic.ttf"},
        {ImTypesetFace_BoldItalic, "Roboto-BoldItalic.ttf"},
        {ImTypesetFace_Code, "Inconsolata-Medium.ttf"},
        {ImTypesetFace_Math, "latinmodern-math.otf"},
    };
    return files;
}

// A headless ImGui context: no window, no display, no renderer. Frames are
// built in memory and composited nowhere.
inline void create_imgui(float display_width, float display_height)
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(display_width, display_height);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |=
        ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
}

inline bool add_fonts()
{
    for (const FontFile& entry : font_files()) {
        if (ImTypeset::AddFontFromFileTTF(entry.face, font_path(entry.file).c_str()) == nullptr) {
            std::fprintf(stderr, "%s: %s\n", entry.file, ImTypeset::GetLastError()->Message);
            return false;
        }
    }
    return true;
}

// The reference's body style: Roboto at a 16 px em, 1.35 leading, justified.
inline void reference_style()
{
    ImTypesetStyle& style = ImTypeset::GetStyle();
    style = ImTypesetStyle();
    style.EmSize = EM_PX;
    style.PixelsPerInch = static_cast<float>(PX_PER_INCH);
}

// `--only NAME` on the command line runs the one section of that name; without
// it every section runs, except those named by `--skip NAME`.
struct Sections {
    std::vector<std::string> only;
    std::vector<std::string> skip;

    Sections(int argc, char** argv)
    {
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::strcmp(argv[i], "--only") == 0)
                only.push_back(argv[++i]);
            else if (std::strcmp(argv[i], "--skip") == 0)
                skip.push_back(argv[++i]);
        }
    }
    bool wants(const char* name) const
    {
        if (std::find(skip.begin(), skip.end(), name) != skip.end())
            return false;
        return only.empty() || std::find(only.begin(), only.end(), name) != only.end();
    }
};

// The paragraphs of bench/data/sample.txt: one per line, blank lines between.
inline bool load_sample(std::vector<std::string>& paragraphs)
{
    const char* path = IMTYPESET_BENCH_DATA_DIR "/sample.txt";
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        std::fprintf(stderr,
                     "%s is missing; write it with\n  python3 tools/sample_text.py "
                     "../imgui-adv-text/demo/sample.md bench/data/sample.txt\n",
                     path);
        return false;
    }
    std::string text;
    char buffer[4096];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof buffer, file)) > 0)
        text.append(buffer, got);
    std::fclose(file);
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        if (end > at)
            paragraphs.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return !paragraphs.empty();
}

} // namespace bench
