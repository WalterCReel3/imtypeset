# clang-clean: result

Clang 18.1.3 (Ubuntu 24.04 container, libstdc++ 13, FreeType 2.13.2, CMake 3.28.3),
through `tools/ci/cpp_preset.sh`, on the working tree over 60b2f32. GCC 12.2.0 on the host
(FreeType 2.12.1).

## In one paragraph

Every one of debug, release, asan, stb, wchar16 and amalgamation builds with no warning under
Clang. In each, the suite passed but for one check, `tests/test_device_grid.cpp:2313`, which
compared `IMTYPESET_VERSION` with the literal "0.4.0" while the header had become 0.4.1; that
is not a Clang finding, and the line no longer names a version. **The edited test has been run
under GCC only** (see "Not done"). Before that, Clang's debug and amalgamation builds aborted in
`test_stress_font`: that was a real defect in the library's strict pass, reproduced under GCC,
fixed in `src/opentype.cpp` and held by four structured cases.

## Warnings, and how each was fixed

| where | warning | fix |
|---|---|---|
| `tests/test_device_advances.cpp:443` | `-Wunused-but-set-variable` `ruler_start` | deleted with the loop that summed it; nothing read it |
| `tests/test_device_advances.cpp:1397` | `-Wsign-conversion`, `const char` to `const unsigned int` in `for (const unsigned int c : {'M', 'i', ' ', '0'})` | the list is of codepoints, which is what the loop passes on: `{0x4Du, 0x69u, 0x20u, 0x30u}` with the letters in a comment |
| `tests/test_linebreak.cpp:101-102` | `-Wunused-const-variable` `STIFF`, `FIL` | deleted; no test uses them |
| `tests/test_harden.cpp:1207` | `-Wsign-conversion`, `std::count`'s `difference_type` subtracted from `size()` | a count is never negative: named `const size_t spaces = static_cast<size_t>(std::count(...))` |
| `tests/test_stress_font.cpp:3127` | `-Wunused-but-set-variable` `strict_refusals` | deleted; the census the test prints already counts strict refusals |
| `src/math_atoms.h` (seen as `dist/imtypeset.cpp` in the amalgamation) | `-Wunused-const-variable` `ATOM_TYPE_COUNT`, `FIELD_TYPE_COUNT` | deleted; nothing in the tree reads either |
| the same | `-Wunused-const-variable` `NEGATIVE_THIN_MU`, `QUAD_MU`, `QQUAD_MU` | these are read by `tests/test_math_mlist.cpp` and not by the library, which gets the numbers from the generated command table. The six `MuGlue` constants are now `inline constexpr`, with a comment saying why: in the drop-in file the header is the translation unit's own text, where an internal-linkage constant nothing reads is unused to Clang |

Those are all there were: the debug build with `-k 0` showed six, the amalgamation five. No
`-Wno-...`, no cast to silence.

Two more corrections found by reading, both in tests:

- `tests/test_stress_font.cpp`: the shorter fuzz for a sanitiser build was selected by
  `__SANITIZE_ADDRESS__`, which GCC defines and Clang 18 does not (checked:
  `clang -fsanitize=address -dM -E` shows no such macro). Clang's asan build ran the full
  count, 247 s for that test. It now also asks `__has_feature(address_sanitizer)`. Not re-timed
  under Clang after the change in isolation; the last asan run (303 s for the whole suite, was
  457 s) includes it.
- `tests/font_builders.h`, `peak_rss_mb()`: `ru_maxrss` is kilobytes on Linux and bytes on
  macOS; the function divided by 1024 on both, and two checks bound it (`all_rss < 768.0`,
  `rss < 16.0`). Now divides by 1024 * 1024 under `__APPLE__`. **Not run on macOS**; from the
  getrusage manual page, not from a measurement.

## The finding: a 596-byte font that reaches ImGui's assertion

**What happened.** Under Clang, debug and amalgamation, `test_stress_font` aborted:

    test_stress_font: .deps/imgui/misc/freetype/imgui_freetype.cpp:540: bool
    ImGui_ImplFreeType_FontBakedLoadGlyph(...): Assertion `pack_id != -1 && "Out of texture
    memory."' failed.

in `fuzz_small_synthetic_fonts_densely_all_the_way` (gdb: `w = 1046, h = 3`, codepoint 65).
Release, stb and wchar16 passed (no assertion, no FreeType, another atlas).

**Why Clang only.** Not code generation. The fuzz draws several random numbers inside one
call's arguments, e.g. `write_field(file, base + 2 * rng.below(12), rng.chance(4) ? 4 : 2,
absurd(rng))`; the order is unspecified, GCC evaluates right to left and Clang left to right,
so the two compilers fuzz different mutants from the same seed. The same pattern is in
`test_selection`, `test_stress_text`, `test_stress_layout`, `test_linebreak_totals`,
`test_blocks`, `test_columns`. **Left as it is, deliberately**: no test depends on the stream,
and the second stream is what found this. The cost is that a fuzz failure on one compiler does
not reproduce on the other without the dumped file (`IMTYPESET_STRESS_DUMP`).

**What it is.** The mutant is saved as `.concerns/clang-clean--glyf-lsb-7fff.ttf`: six
squares, 1,000 units per em, and one changed field, the `hmtx` left side bearing of glyph 1 =
0x7FFF. It passed the reader and the strict pass. FreeType moves a `glyf` glyph by
`lsb - xMin`; with the moved coordinates past 32,767 its autohinter returns an outline 65,536
units wide. Measured with FreeType alone (a scratch probe, not in the tree), 16 px:
`FT_LOAD_TARGET_NORMAL` gives a bitmap 1046 x 3; `TARGET_LIGHT`, `NO_HINTING` and
`NO_AUTOHINT` give 3 x 3. Bisected on the side bearing: 32,600 is clean, 32,660 aborts (the
glyph's points reach x = 150). Reproduced by replaying the file
(`IMTYPESET_STRESS_FONT_LIST`) with GCC 12 + FreeType 2.12.1 on the host and GCC 13 +
FreeType 2.13.2 in the container. It depends on the glyph's shape: the 500-unit square of
`three_squares` with the same bearing gives 9 x 8, no assertion.

This is a fourth route to the assertion `docs/hardening.md` item 4 records, and unlike the
`fpgm` route the strict pass can bound it.

**The fix (a decision of mine; confirm or overturn).** `src/opentype.cpp`,
`OpenTypeFont::strict_glyf`: after a glyph passes, with `moved = reach_x + |lsb - xMin|`,

- `moved > 32767` refuses with "a glyph's side bearing moves its outline past 16 bits";
- `moved > STRICT_REACH * extent` refuses with "a glyph's side bearing moves its outline far
  outside the font's height" (the reach rule, applied where the glyph is drawn: `draw_glyf`
  moves layout's boxes the same way).

It applies to every glyph with data, composites included. A glyph with no data is not moved.

**The tests that hold it.** `tests/test_stress_font.cpp`, `structured_cases()`, four cases
after "advance width of 33,368 units": side bearing 0x7FFF, 0x8000 and 15,600 expect
`STRICT_REFUSED`; 15,500 (exactly sixteen heights out) expects `ACCEPTED`. The three refusals
failed before the fix ("outcome 4, expected 1"), as checks and not as an abort. The saved
mutant is refused by the strict pass after it.

**What it costs real fonts.** `IMTYPESET_STRESS_FONT_LIST` over the 2,925 font files under
`/usr/share/fonts` (2,991 faces as shipped, `IMTYPESET_STRESS_NO_HINTING=1`): 0 refused by the
strict pass before (2,989 faces read in the container build) and 0 after.

**Not covered, and said so.**

- A composite whose component lends it its metrics (`USE_MY_METRICS`) is moved by the
  component's bearing, which the rule does not add. Probed with FreeType 2.12.1 on three
  shapes with moved coordinates up to 33,050: the composite is moved (left 521 px) and no wide
  bitmap came back. Three shapes are not a proof.
- That FreeType's autohinter holds coordinates in 16 bits is inferred from the threshold, not
  read in its source.
- Running the env-driven test over installed fonts WITH hinting still aborts, on a mutant of
  NotoSansArmenian-Thin whose `fpgm` bytes changed: that is route (b) of hardening.md item 4,
  recorded there as not fixable by the strict pass. Not new.
- FreeType 2.14 (what Homebrew installs) has not met any of this.

**Text for the shared documents** (docs/ was another task's; only the CHANGELOG line is in):

- `docs/decisions.md`, a new entry: "The strict pass bounds a glyf glyph where it is drawn.
  A glyph's outline moved by `lsb - xMin` must stay within 16 bits and within `STRICT_REACH`.
  Measured: a 596-byte font with one side bearing of 32,767 gave a 1046 x 3 px bitmap at 16 px
  under FreeType 2.12.1 and 2.13.2 with `FT_LOAD_TARGET_NORMAL`, and ImGui's 'Out of texture
  memory' assertion; 0 of 2,991 installed faces refused. Not measured: FreeType 2.14;
  `USE_MY_METRICS` beyond three shapes."
- `docs/hardening.md`, item 4: a route (c'), a side bearing, closed by the strict pass.
- `docs/font-support.md`: a row under what is refused, with the two reasons above.
- `docs/CHANGELOG.md` 0.4.1 says "no name, no meaning and no layout differs from 0.4.0". I
  added a **fixed** entry under it for this refusal; the sentence above it is now true except
  for that entry, and I did not reword it.

## ctest

Clang 18.1.3, each through `tools/ci/cpp_preset.sh <preset>` into `build/clang-clean-<preset>`,
the final tree but for the one line of `test_device_grid.cpp`:

| preset | built | passed | failed |
|---|---|---|---|
| debug | clean | 54 of 55 | `test_device_grid`, the version literal |
| release | clean | 53 of 54 | the same |
| asan | clean | 53 of 54 | the same; no sanitiser report |
| stb | clean | 53 of 54 | the same |
| wchar16 | clean | 53 of 54 | the same |
| amalgamation | clean | 53 of 54 | the same |

The one failure, in every preset:

    tests/test_device_grid.cpp:2313: [a_host_compiled_against_0_3_0_is_refused_by_its_version_and_by_its_struct_sizes]
    CHECK_EQ(std::string(IMTYPESET_VERSION), std::string("0.4.0"))
        left:  "0.4.1"
        right: "0.4.0"

Fixed since: the line is `CHECK(IMTYPESET_VERSION_NUM >= 400)`, the test's premise, and the
checks that a 0.3.0 host is refused are untouched.

Also Clang with **libc++ 18** in place of libstdc++ (debug, `-stdlib=libc++`, no consumer
test): built clean, 53 of 54, the same one failure. Run before the last comment-only edits.

GCC 12.2.0 on the host, `cmake --preset debug -B build/clang-clean-gcc`: built clean; full
ctest 54 of 55 with the same one failure; then, after the fix to that line,
`test_device_grid` alone: `ok: 19 test(s), 31338 check(s), 0 failed check(s)`.

`-ffp-contract=off` is on every compile command of the Clang debug, release, asan (88 each)
and amalgamation (68) builds, ImGui's sources included; no `-ffast-math`, `-Ofast` or
`-funsafe-math-optimizations` anywhere. The consumer-project test's own sub-builds were not
inspected.

## Not done

- **`test_device_grid` after its fix has not been run under Clang.** The call was refused by
  the operator ("Check with the lead first"); I asked the lead and had no answer when this was
  written. The change is one integer comparison of a macro, compiled and run by GCC only.
- The six Clang presets were not run again after that line changed, as instructed.
- GCC: only debug. release, asan, stb, wchar16, amalgamation were not rebuilt with GCC after
  these edits, and `src/opentype.cpp` changed.
- `fp-host`, `exceptions`, `bundle-config`, `imgui-next`, `python-host`: not built under Clang.
- `build/clang-clean*` is still there (logs, the image's Dockerfile, the helper): about a
  dozen directories to remove once this has been read.
- A test that runs a decimal-comma locale for real (`test_harden`, `DecimalComma`): I meant to
  generate a locale in the container and run it; not done.

## macOS watch-list

What Linux Clang could and could not stand in for.

Checked here, so less likely to be the cause there:

1. **Apple's typedefs** (`int64_t` is `long long`, `uint64_t` is `unsigned long long`, neither
   is `long` or `size_t`). 30 translation units had never been compiled on macOS when the two
   CI runs stopped (`test_device_grid`, `test_mathtext`, `test_nodes`, `test_opentype*`,
   `test_pack`, `test_paragraph`, `test_selection`, `test_shipout`, `test_soft_raster`,
   `test_specimen`, `test_stress_*`, `test_style`, `test_tex_rules`, `test_units`,
   `test_vsplit`, `test_word_gaps`, `smoke`, every bench and example). All 82 C++ translation
   units were syntax-checked under Clang with those two types and the `PRI*64` macros swapped
   for macOS's (`build/clang-clean-docker/mac_types.h`, `mac_types.py`): no diagnostic. A stand-in,
   not Apple's headers; a probe with `printf("%ld", int64_t)` and `std::min(size_t, uint64_t)`
   does fail under it.
2. **libc++** (sort of equal elements, hash order, allocation counts in `test_linebreak`,
   stricter transitive includes): the whole suite passes on Linux with libc++ 18. Apple's is
   older (Xcode 15).
3. **Float-to-integer conversions out of range**, which x86 and ARM answer differently:
   Clang's `-fsanitize=undefined` includes that check (GCC's does not) and the asan preset
   reported nothing.
4. **libm**: `src/` calls only `sqrt`, `floor`, `ceil`, `fabs`, `ldexp`, `nearbyint`,
   `nextafter`, all exact or correctly rounded by IEEE 754. No `pow`, `exp`, `log`, `sin` in
   `src/`, `tests/`, `bench/` or `examples/`.
5. **Golden floats**: read by `strtod` from hex, which is exact; written for comparison by
   `core_golden::hex_float`, by hand from the bits, so `printf("%a")`'s differences between C
   libraries do not matter, and a NaN is "nan" whatever its sign (x86 and ARM make NaNs of
   opposite sign). `%a` appears otherwise only in failure messages.
6. **Unordered containers** in `src/` are iterated in four places, none of them order-dependent.

Not checkable here; read these first if a macOS run fails:

7. **FreeType 2.14.3** (Homebrew) against 2.12.1 and 2.13.2 here. Anything that compares what
   the rasteriser made: `test_device_advances`, `test_device_grid`, `test_hidpi`,
   `test_font_loader*`, `test_api_math` (inked pixel counts), `test_word_gaps`, `test_demo`,
   `test_stress_font`.
8. **`ru_maxrss` in bytes** (fixed above from the manual page, unrun): `test_stress_font`,
   the N1 and N2 cases.
9. **Wall-clock bounds** on a slower or busier runner: `test_stress_layout` (5, 10, 60 s),
   `test_stress_font` (5 and 10 s, and `seconds_since(again) < first + 0.05` at line 1992,
   the tightest), `test_vsplit` (20 s), `test_stress_text` (90 s).
10. **Locales**: `test_harden` runs its decimal-comma tests only where `de_DE.UTF-8` or a
    like locale exists. This machine and the container have none, so they are skipped here;
    macOS ships them, so they may run there for the first time.
11. **Sanitisers**: `test_linebreak`, `test_exception_safety` and others replace global
    `operator new`; under ASan on macOS that goes through a different mechanism (an
    interposing runtime library) than on Linux. `tools/ci/cpp_preset.sh` sets
    `ASAN_OPTIONS=hard_rss_limit_mb=3000`; whether Apple's runtime honours it was not checked.
12. **Fuzz streams** follow the compiler's argument order (above). Apple Clang should draw the
    stream Linux Clang draws, which now passes; if it does not, set `IMTYPESET_STRESS_DUMP`.
13. `std::sort` with a key that can tie: `src/font_loader.cpp:102` (cmap groups by first
    code), `src/imtypeset.cpp:685` (formulas by start), `std::nth_element` in
    `src/imtypeset_document.cpp:1792` (views by last use). Ties are excluded by construction
    in the first two; the third decides which of two equally old views is dropped.
14. Not macOS but the same family: **`char` is unsigned on Linux aarch64**, signed on x86-64
    and on Apple Silicon. Not audited; it matters only if a Linux aarch64 wheel is planned.
15. Apple Clang 15 is older than Clang 18 and may warn where 18 does not; the two failed CI
    logs show the same warnings from both for what each reached.

## To repeat it

    mkdir -p build/clang-clean-docker && cd build/clang-clean-docker
    cat > Dockerfile <<'EOF'
    FROM ubuntu:24.04
    RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        clang libclang-rt-18-dev llvm-18 cmake ninja-build libfreetype-dev pkg-config git python3 ca-certificates \
        make libc6-dev libstdc++-13-dev g++ libc++-18-dev libc++abi-18-dev gdb \
        && rm -rf /var/lib/apt/lists/*
    EOF
    docker build -t imtypeset-clang:24.04 .

`libclang-rt-18-dev` is needed for the asan preset: without it the compiler check fails at
link (`libclang_rt.asan-x86_64.a` not found). The first `docker pull` failed on a DNS lookup
and worked on the second try.

One preset, from the repository root (the repository is mounted at its own path, so the build
directories are the host's):

    repo=$PWD
    docker run --rm --memory=6g --cpus=4 -u "$(id -u):$(id -g)" -e HOME=/tmp \
        -e CC=clang -e CXX=clang++ -e IMTYPESET_CI_JOBS=4 -e ASAN_OPTIONS=hard_rss_limit_mb=3000 \
        -v "$repo:$repo" -w "$repo" imtypeset-clang:24.04 \
        sh -c "IMTYPESET_CI_BUILD_DIR=$repo/build/clang-clean-debug sh tools/ci/cpp_preset.sh debug"

and the same with `release`, `asan`, `stb`, `wchar16`, `amalgamation`. To see every warning at
once, in place of the script: `cmake --preset debug -B build/clang-clean-debug && cmake --build
build/clang-clean-debug -j 4 -- -k 0`. libc++: add
`-e IMTYPESET_CI_CMAKE_ARGS="-DCMAKE_CXX_FLAGS=-stdlib=libc++ -DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ -DIMTYPESET_BUILD_CONSUMER_TESTS=OFF"`
and another build directory. The typedef stand-in: `python3 build/clang-clean-docker/mac_types.py
build/clang-clean-debug $PWD/build/clang-clean-docker/mac_types.h` inside the container, after
a debug build.

## Files changed

`src/math_atoms.h`, `src/opentype.cpp`, `tests/font_builders.h`,
`tests/test_device_advances.cpp`, `tests/test_device_grid.cpp`, `tests/test_harden.cpp`,
`tests/test_linebreak.cpp`, `tests/test_stress_font.cpp`, `docs/CHANGELOG.md` (one section,
two entries, at the lead's instruction). New: `.concerns/clang-clean--glyf-lsb-7fff.ttf`, this
file. Nothing staged or committed.
