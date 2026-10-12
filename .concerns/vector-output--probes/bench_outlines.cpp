// Scratch probe for docs/vector-output.md: does the reader already walk the
// outline of every glyph of the seven test fonts, glyf and CFF, and what does
// a walk cost? Uses the internal reader (src/opentype.h); not part of the
// library.
#include "bench_common.h"
#include "opentype.h"

#include <cstdint>

int main()
{
    static const char* const files[] = {"Roboto-Regular.ttf",       "Roboto-Bold.ttf",
                                        "Roboto-RegularItalic.ttf", "Roboto-BoldItalic.ttf",
                                        "Inconsolata-Medium.ttf",   "DroidSans.ttf",
                                        "latinmodern-math.otf"};
    std::printf("| font | outlines | glyphs | with ink | walk all outlines once, ms | us per glyph "
                "with ink |\n|---|---|---|---|---|---|\n");
    for (const char* file : files) {
        std::vector<uint8_t> bytes;
        if (std::FILE* f = std::fopen(bench::font_path(file).c_str(), "rb")) {
            uint8_t buffer[65536];
            size_t got = 0;
            while ((got = std::fread(buffer, 1, sizeof buffer, f)) > 0)
                bytes.insert(bytes.end(), buffer, buffer + got);
            std::fclose(f);
        }
        double best = 1.0e30;
        uint32_t count = 0, inked = 0;
        bool cff = false;
        for (int pass = 0; pass < 5; ++pass) {
            ImTypeset::detail::OpenTypeFont font; // a fresh one: bounds are remembered per font
            std::string why;
            if (!font.load(bytes.data(), bytes.size(), &why)) {
                std::printf("| %s | refused: %s |\n", file, why.c_str());
                break;
            }
            cff = font.is_cff();
            count = font.glyph_count();
            inked = 0;
            const double start = bench::now_us();
            for (uint32_t g = 0; g < count; ++g) {
                const ImTypeset::detail::GlyphBounds b = font.bounds(g);
                if (b.x_max > b.x_min || b.y_max > b.y_min)
                    ++inked;
            }
            best = std::min(best, bench::now_us() - start);
        }
        std::printf("| %s | %s | %u | %u | %.2f | %.2f |\n", file, cff ? "CFF" : "glyf", count,
                    inked, best * 1.0e-3, inked ? best / inked : 0.0);
    }
    const std::string load = bench::first_line_of("/proc/loadavg", "");
    std::printf("\nbest of 5; %s %s; load average %s\n", IMTYPESET_BENCH_COMPILER,
                IMTYPESET_BENCH_BUILD_TYPE, load.c_str());
    return 0;
}
