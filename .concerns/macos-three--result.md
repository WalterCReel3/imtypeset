# macos-three: result

Working tree over cf7466e (branch `publish-0.4.1`). GCC 12.2.0 and FreeType 2.12.1 on the
host; Clang 18.1.3, libstdc++ 13 or libc++ 18, FreeType 2.13.2 in the container
`imtypeset-clang:24.04`; FreeType 2.14.3 built there from the release tarball. Nothing was run
on macOS.

## In one paragraph

The first macOS run (Actions run 38338953414) reported three failing tests in every preset and,
in the asan preset only, a fourth that the board entry does not name (`test_stress_font`
aborted). **All four are faults of the tests; none is a defect of the library, and `src/` is
unchanged.** Each was reproduced on this Linux machine before anything was edited. A Mac
user's Cmd+C copies; no layout depends on the locale; the two glyph boxes are FreeType 2.14.3's
and no advance moves with them.

## 1. The copy shortcut (`test_api`)

**What it was.** The test injected `io.AddKeyEvent(ImGuiMod_Ctrl, true)`. Read in the pinned
Dear ImGui v1.92.9:

- `imgui.cpp:1688`: `ConfigMacOSXBehaviors = true` when compiled under `__APPLE__`.
- `imgui.cpp:1889-1897`, `ImGuiIO::AddKeyAnalogEvent`: under that setting `ImGuiMod_Ctrl` and
  `ImGuiMod_Super` (and the left and right Ctrl and Super keys) are exchanged as events arrive.
- `imgui.h:1685`: `ImGuiMod_Ctrl` is "Ctrl (non-macOS), Cmd (macOS)".
- `imgui_widgets.cpp:5144, 5148`: `InputText` asks for copy as `ImGuiMod_Ctrl | ImGuiKey_C`
  and select-all as `ImGuiMod_Ctrl | ImGuiKey_A` on every platform.

A backend submits the Cmd key as `ImGuiMod_Super`. The library asks `IsKeyChordPressed(
ImGuiMod_Ctrl | ImGuiKey_C)` and `... | ImGuiKey_A` (`src/imtypeset.cpp:1740-1742`,
`src/imtypeset_document.cpp:4132-4137`): the chord ImGui's own widget asks for. So on a Mac the
library copies on Cmd+C, and the test pressed the physical Ctrl key, which there is
`ImGuiMod_Super` and copies nothing, in ImGui's text fields too.

**Reproduced.** With `io.ConfigMacOSXBehaviors = true` on Linux and the old injection: the
same four checks fail with the runner's values (clipboard not written; selection still 10 to
15; clipboard not written after select-all).

**Verdict.** Test fault. The hypothesis "the library asks in a way that does not follow ImGui's
convention" is refuted.

**Changed.**

- `tests/test_api.cpp`: the scenario is a function run under both settings on every platform
  (`selectable_text_selects_with_the_mouse_and_copies_to_the_clipboard`: off;
  `selectable_text_copies_with_cmd_under_mac_behaviors`: on;
  `selectable_text_copies_with_the_shortcut_of_the_platform_it_was_built_for`: ImGui's own
  default, which it also asserts is on under `__APPLE__` and off elsewhere). It injects what a
  backend submits for the platform's shortcut key (`ImGuiMod_Super` under the Mac setting), and
  asserts that the OTHER modifier with C and with A copies and selects nothing.
- `tests/test_api_document.cpp`: `the_view_selects_all_and_copies_with_the_shortcuts_of_ctrl`
  and `..._of_cmd_under_mac_behaviors`. `DocumentView()`'s keyboard path had no C++ test.
- `include/imtypeset.h`: the comments of `SelectableText` and `DocumentView` say which key it
  is on a Mac and why. Comments only.
- No probe row: `src/` reads no new constant.

**Not changed, outside this task's files, and wrong on a Mac in the same way:**

- `python/host/imtypeset_host.cpp:400-407`, `ImTypesetHost_ScriptCopyShortcut`: injects
  `ImGuiMod_Ctrl`. On a Mac the scripted "copy" presses Ctrl, so
  `tests/python/test_binding.py:585, 1318` and `examples/python/minimal_host.py:63` would see
  no copy if they are ever run there. Fix: `ImGui::GetIO().ConfigMacOSXBehaviors ?
  ImGuiMod_Super : ImGuiMod_Ctrl`.
- `examples/demo_headless.cpp:294-298`, `script_math`: Ctrl+A into an `InputText`. On macOS
  the select-all does not happen and the typed formula is appended to the source instead of
  replacing it. `example_demo_headless` passed on the runner all the same, so its `expect`s do
  not notice; the page it draws there is not the one intended.
- The library has no Ctrl+Insert (ImGui's text field also copies on it). Not a defect; said
  because "exactly as ImGui's own widgets do" is true of the two chords, not of this third.

**Next macOS run:** `test_api` passes, with three `selectable_text_...` tests in it;
`test_api_document` passes with the two new ones.

## 2. "A decimal comma changes the layout" (`test_harden`, h8)

**What it was.** The test compared two `std::vector<DrawCmd>` with `memcmp`. `DrawCmd`
(`src/shipout.h`) is a `uint8_t` `type` followed by a `uint32_t`: three bytes of padding that
nothing writes. Nothing about a locale.

**Reproduced.** `locale -a` here: `C`, `C.utf8`, `en_US.utf8`, `POSIX`, so the comparison had
never run on this machine or in the container: the test skipped. With a locale compiled
privately (`localedef -i de_DE -f UTF-8 build/macos-three-loc/de_DE.UTF-8`, run with
`LOCPATH`):

| build | h8 | fields differing | commands with non-zero padding | commands whose padding differs |
|---|---|---|---|---|
| GCC 12 debug | passes, locale really set (`LC_NUMERIC = de_DE.UTF-8`) | 0 of 139 | 0 | 0 |
| Clang 18 + libc++ debug | **fails, `different` = 35** | 0 of 139 | 135 | 35 |

The runner's counts were 4, 81, 30 and 75 depending on the preset, which a locale would not
do and uninitialised bytes would.

**Verdict.** Test fault. The library lays the paragraph out identically under `de_DE.UTF-8`
with glibc: every field of every command, and the size.

**The audit asked for** (locale-sensitive calls in `src/` and `include/`, by grep and read):

- `decimal_of` (`src/imtypeset.cpp:111-117`): `snprintf("%.7g")` then `strtod`, both in the
  current locale, so the text is "1,35" and is read back as 1.35. The one place a decimal is
  printed or parsed. Unchanged (D39's H8). It relies on a C library whose `strtod` reads its
  own `snprintf`; glibc does (measured above).
- `TextV`: the host's own format string through `vsnprintf`, as `ImGui::Text` does.
- Every other `snprintf`/`vsnprintf` format in `src/` has only `%s`, `%d`, `%u`, `%04X`,
  `%05u`, `PRId64` (grep for a floating conversion finds `decimal_of` alone, outside the demo
  window, whose 23 are labels on screen); `std::to_string` is called on integers only.
- No `<cctype>`, `<clocale>`, `<locale>`, `<cwctype>` in `src/` or `include/`; no `isspace`,
  `isalpha`, `toupper`, `strcoll`, `atof`, `sscanf`, `std::stod`.

**Changed.** `tests/test_harden.cpp` h8 and `tests/test_exception_safety.cpp` (two places):
`DrawCmd`'s own `operator==` in place of `memcmp`. h8 also lays out twice in the process's own
locale and compares, so the comparison runs on every machine, and prints the first differing
command field by field and the locale it set. `docs/hardening.md` H8 row.

**Next macOS run:** h8 passes and prints `LC_NUMERIC = de_DE.UTF-8`. If it fails, it now
prints the command and the fields, and that would be Apple's C library not reading back its
own decimal in `decimal_of`: a real defect, and the one thing here not measurable from Linux.

**For the lead:** the Linux CI jobs have no decimal-comma locale either, so h3's last part and
h8's second half are skipped there. Compiling one in the workflow (`localedef` as above, and
`LOCPATH`) would run them; `.github/workflows` is not this task's.

## 3. Two glyph boxes of 7,510 under FreeType 2.14.3 (`test_font_loader`)

**Reproduced.** FreeType 2.14.3 from `download.savannah.gnu.org`, built static in the
container (`build/macos-three-ft`), test built against it with the same Clang and Dear ImGui:
2 of 7,510, as on the runner.

    code at 16 px, U+0022: the reference's box is (1, 4)-(6, 8), this build's (2, 4)-(6, 8)
    code at 37 px, U+0022: the reference's box is (4, 8)-(13, 17), this build's (4, 8)-(14, 17)

Inconsolata-Medium's quotation mark, one pixel on one edge. The golden's `advance_x`, which
the test did not compare before: 0 of 7,510 differ under 2.12.1, 2.13.2 and 2.14.3. Ascent and
descent: 0 of 30 sizes under all three. So nothing layout reads moves.

**Verdict.** FreeType's, not the port's. No tolerance, nothing regenerated.

**Changed** (`tests/test_font_loader.cpp`; D7 amended):

- The test reads the linked FreeType from the loader's name and prints every differing box.
- 2.12.1, 2.13.2, 2.13.3 (measured to reproduce the golden, or the golden's own): every box,
  every advance, ascent and descent, exactly.
- 2.14.3: every box exactly except those two, named by face, size and codepoint (no number of
  this build's is written into the test); the two must differ; advances and metrics exact.
- Any other release: prints the differences and `SKIPPED the box comparison: ...` naming both
  versions.
- stb_truetype: as before, reported, now with the word SKIPPED.

**Next macOS run:** passes, printing "2 of 7510 glyph boxes and 0 advances differ ..." and the
two lines above (wchar16: 2 of 7505). **If Homebrew's 2.14.3 moves other boxes than the
tarball's, or an advance, it fails and prints which**; that is the part I could not see. The
stb preset is untouched by this.

## 4. Not on the board: `test_stress_font` aborted under ASan on macOS

    AddressSanitizer: container-overflow ... WRITE of size 1 ... mutate() test_stress_font.cpp:2477

The fuzz cuts the file in one round (`file.resize`) and in a later round wrote a byte at an
offset of the uncut file: past the vector's size, inside its capacity. libc++ annotates that
for ASan; libstdc++ does not, which is why Linux never saw it. **Reproduced** under GCC with
`-D_GLIBCXX_ASSERTIONS` (`operator[]`'s assertion, in `fuzz_every_bundled_font_...`).
**Test fault**; no library code reads there. Four writes are now bounded
(`poke`, `flip`, the `loca` swap), as `wr16`'s already were, with the random draws in the same
order. After it the whole of `test_stress_font` passes with `-D_GLIBCXX_ASSERTIONS`.

**Next macOS run:** the asan preset's `test_stress_font` gets past `mutate`. Whether something
else stops it further on is unknown: the run aborted 25 s in, before the rest of the test.

## What was run

All on the final tree (the last source edit precedes every run below; only documents changed
after). Every binary under `ulimit -v 6000000` or `ASAN_OPTIONS=hard_rss_limit_mb=3000`,
docker with `--memory=6g --cpus=4`, `-j4`. The German locale was available (`LOCPATH`) in each,
so h3 and h8 ran their decimal-comma halves.

| what | result |
|---|---|
| GCC 12.2.0, `debug` preset, whole suite, FreeType 2.12.1 | 55 of 55 pass |
| Clang 18.1.3 (libstdc++), `debug` preset through `tools/ci/cpp_preset.sh`, whole suite, FreeType 2.13.2 | 55 of 55 pass, no warning |
| Clang 18.1.3 + libc++, ASan + UBSan (the `asan` preset with `-stdlib=libc++` added): `test_api`, `test_api_document`, `test_harden`, `test_font_loader`, `test_stress_font`, `test_exception_safety`, the three examples | 9 of 9 pass, no report. The same build with HEAD's `test_stress_font.cpp` gives the runner's report: `container-overflow`, `test_stress_font.cpp:2477`, `mutate` |
| Clang 18.1.3, debug, FreeType 2.14.3 from the tarball: `test_font_loader` | 8 tests pass; the two boxes printed |
| GCC, `exceptions` preset (ASan + UBSan, a throwing `IM_ASSERT`): `test_exception_safety` | 10 tests, 71,570 checks pass. In every other build this test is one skip, so this is the only run of its two edited comparisons |
| GCC debug with `-D_GLIBCXX_ASSERTIONS`: `test_stress_font` | aborted before the fix, 22 tests pass after |

Not run: release, stb, wchar16, amalgamation, fp-host, bundle-config, imgui-next, python-host,
the Python tests, anything on macOS or under Apple Clang, MSVC. The private build directories
(`build/macos-three*`) are removed.

To repeat the two container reproductions:

    localedef -i de_DE -f UTF-8 <dir>/de_DE.UTF-8          # then LOCPATH=<dir>
    # FreeType 2.14.3: unpack the release tarball, then inside the container
    cmake -S <src> -B <b> -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_PNG=ON \
        -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_ZLIB=ON -DCMAKE_INSTALL_PREFIX=<prefix>
    cmake -S . -B build/<dir> -G Ninja -DCMAKE_PREFIX_PATH=<prefix> \
        -DFREETYPE_LIBRARY=<prefix>/lib/libfreetype.a \
        -DFREETYPE_INCLUDE_DIRS=<prefix>/include/freetype2 -DIMTYPESET_TESTS=test_font_loader
    # libc++ under ASan: the asan preset with
    #   "-DCMAKE_CXX_FLAGS=-stdlib=libc++ -fsanitize=address,undefined -fno-omit-frame-pointer"
    #   "-DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ -fsanitize=address,undefined"
    # (CMAKE_CXX_FLAGS on the command line REPLACES the preset's: with -stdlib=libc++ alone
    # the C++ is not sanitised and everything passes.)

## Priority, as changed while this ran

The owner tags 0.4.1 from cf7466e without these edits; they are the follow-up. So the 0.4.1
macOS jobs will show the same three failures (and the asan abort) again, for the reasons above,
none of them the library's. The changelog lines are under a new `## Unreleased` section, not
under 0.4.1. `docs/advtext-swap-status.md` was not touched.

## Anything else in the macOS log

- The saved log holds only the output of failed tests, so skips of passing tests are not in
  it. In `test_harden`: `SKIPPED: IMTYPESET_HARDEN_SHOTS is not set` (images, by design), and
  h3 printed `LC_NUMERIC = de_DE.UTF-8`: its 314,000 quotients were compared under a decimal
  comma on macOS for the first time, and passed.
- `clang-clean`'s note that "the whole suite passes on Linux with libc++ 18" was a plain
  build; libc++ under ASan is what reports item 4.
- Watch-list items 8 (`ru_maxrss`), 9 (wall-clock bounds), 12 (fuzz streams) and 13 (sort
  ties) of `.concerns/clang-clean--result.md`: nothing in the run points at any of them.

## Files changed

`include/imtypeset.h` (comments), `tests/test_api.cpp`, `tests/test_api_document.cpp`,
`tests/test_harden.cpp`, `tests/test_exception_safety.cpp`, `tests/test_font_loader.cpp`,
`tests/test_stress_font.cpp`, `docs/decisions.md` (D7 amended, D84 new), `docs/hardening.md`,
`docs/CHANGELOG.md`, this file. Nothing staged or committed.

**One thing for the lead to look at before tagging.** Commit 5142d93 ("Name v0.4.1 for
imscribe; say what is known to fail on macOS") took my `## Unreleased` section of
`docs/CHANGELOG.md` in with its own edit: the section was in the working tree when the file
was added. As committed, the changelog (a) says the tests "now press each platform's key" and
cites D84, while the tests and D84 are still uncommitted, and (b) says under Unreleased that
all four are test faults while "Known when released" under 0.4.1 says that is "not yet
established". It is established (above). Either drop the Unreleased section from that commit
until the rest lands, or let "Known when released" say what is now known: the three failures
are faults of the tests, reproduced on Linux, and a Mac user's Cmd+C copies. I have not
touched the committed text.
