// Experiment A, second half: write to a file the font src/font_loader hands to
// an ImGui atlas for a face — the font file with its cmap rewritten in memory
// (docs/decisions.md D27) — and print the codepoints minted for it, so that a
// Python process can hand those bytes to the installed imgui_bundle's atlas and
// look for a minted codepoint there.
//
//   expa_write_atlas_font FONT OUT [WCHAR_BITS=32]
//
// Standalone: linked with this repository's own ImGui (.deps/imgui), nothing
// of the bundle. Prints one JSON object.

#include "font_loader.h"
#include "fontlib.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    using namespace ImTypeset::detail;
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s FONT OUT [WCHAR_BITS]\n", argv[0]);
        return 2;
    }
    const int wchar_bits = argc > 3 ? std::atoi(argv[3]) : 32;
    FontLibrary library;
    std::string why;
    if (!library.register_font(Face::MATH, argv[1], &why)) {
        std::fprintf(stderr, "register_font: %s\n", why.c_str());
        return 1;
    }
    const FaceData* face = library.face_data(Face::MATH, &why);
    if (face == nullptr) {
        std::fprintf(stderr, "face_data: %s\n", why.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes;
    if (!build_atlas_font(*face, wchar_bits, bytes, &why)) {
        std::fprintf(stderr, "build_atlas_font: %s\n", why.c_str());
        return 1;
    }
    std::FILE* f = std::fopen(argv[2], "wb");
    if (f == nullptr || std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    std::fclose(f);

    const std::vector<SyntheticCodepoint>& minted = face->synthetic_codepoints();
    std::printf("{\"bytes\": %zu, \"wchar_bits\": %d, \"minted_count\": %zu, \"minted\": [",
                bytes.size(), wchar_bits, minted.size());
    for (size_t i = 0; i < minted.size(); ++i)
        std::printf("%s[%u, %u]", i ? ", " : "", minted[i].codepoint, minted[i].glyph);
    std::printf("]}\n");
    return 0;
}
