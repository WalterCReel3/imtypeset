# demo-guard — result (builder, 2026-10-10)

Files changed: `src/imtypeset_demo.cpp`, `tests/test_demo.cpp`, `examples/demo_headless.cpp`.
Nothing of mine in `include/`, `src/imtypeset.cpp` or any CMake file. No `git add`.
`build/demo-guard*` removed. Every binary ran under `ulimit -v 6000000; timeout 600` (sanitised:
`ASAN_OPTIONS=hard_rss_limit_mb=3000 timeout 600|900`), builds `-j3`, one sanitised build at a time.

## Short form

All three findings are real: each was reproduced by running, against the unmodified demo, before
anything was changed. All three are fixed at the cause. The demo no longer infers which context it
is in: it asks. That needed one public declaration, `ImTypeset::GetContextSerial()`, which I
requested from the lead and which `doc-fixes-now`'s builder added (header, C counterpart, map row,
Python binding — their work, not mine; I only call it).

History of the task, because the progress file shows two designs: I first built the best fix the
public API then allowed (state keyed by the context pointer, carried only across consecutive
frames, handles destroyed whenever the window's contents were not shown). It left one sequence open
and cost a small leak and a reset after every pause. When the serial landed I replaced it; nothing
of the first design is in the tree.

## Per finding

### F4a — use of a dead handle after the context is replaced at the same address: REPRODUCED, FIXED

Reproduced with `a_context_made_at_the_address_of_a_dead_one_gets_handles_of_its_own` (all sections
open; window collapsed; `DestroyContext` + `CreateContext` under the same ImGui context; the host
typesets until the new context's `TextBuilds` passes the old one's; window expanded), on the
unmodified demo, ASan+UBSan FreeType/32-bit:
- ASan defaults: the new context lands at ANOTHER address (the freed one sits in quarantine), the
  old pointer compare catches it and the test passes — which is why the existing four-context test
  never showed the defect;
- `ASAN_OPTIONS=quarantine_size_mb=0:thread_local_quarantine_size_kb=0`: same address, and
  `SEGV in ImTypeset::detail::core::bound <- LayoutParagraph <- ParagraphItem <- ShowDemoFaces`.

Fixed: `[SECTION] One state per context`. Everything the demo remembers is in a `DemoState`, found
by `ImTypeset::GetContextSerial()` of the context the window is shown in. A handle is used only in
the context it was made in, and the demo never destroys one: it dies with its context. No pointer
compare, no debug counter, no frame counting.

Tests: the one above (now also passes with quarantine off, at the same address);
`a_context_replaced_between_two_frames_that_show_the_demo_gets_handles_of_its_own` — the sequence
my first design could not close: twelve times `DestroyContext` + `CreateContext` with no frame and
no typesetting in between, the demo shown before and after. It prints how often the new context was
at the old address: **11 of 12 in release**; see the table for the sanitised runs. NOT done: this
test was never run against the first design or the original demo (it was written after both were
gone), so that it fails without the serial is by reasoning, not by observation.

### F4b — two live contexts leak: REPRODUCED, FIXED

`two_live_contexts_showing_the_demo_each_keep_one_set_of_handles`: two hosts, each its own ImGui
and ImTypeset context, alternating frames; handles counted from `ImTypesetContext::paragraphs` and
`documents` (`src/context.h`). Unmodified demo: 5 expected per context, **25 after 6 frames, 125
after 26**, and 200 layouts in 20 frames in which nothing changed. Now 5 and 5, and 0 layouts once
settled: each context finds its own state, and what is done in one is not seen in the other.

### F5 — test_demo order dependence through file statics: REPRODUCED, FIXED

The reviewer's moderate confidence was justified.
`what_was_done_to_the_demo_in_one_context_is_not_there_in_the_next` measures how far the demo
window scrolls with everything open in a fresh host, drags across every section in a second host,
measures again in a third. Unmodified demo, the test run ALONE (`--filter`): **3894 px fresh, 9140
px in the third host**. In the full run the same test PASSED, because the earlier drag test had
dirtied the statics before "fresh" was measured — the order dependence itself.

What I chose: no reset hook (it would need a compile definition on the library: a CMake edit, not
mine, and a second build shape). The demo has no remembered statics left. Each section's controls
are in a `DemoXxxState` struct declared just above the section, defaults as member initialisers;
`DemoState` holds one of each. A new context is a new demo. `g_style`, `g_font_size`, `g_frame`
remain file statics but are written at the top of every call and carry nothing; the function
statics left (`text`, `rects`) are scratch emptied before each use, and `hits` is a constant
computed once. About 45 statics moved — more than the guard alone needed; taken also because the
Stress section's measured copy height and the Document section's handle and counters were statics
shared by every context, which is wrong for two live contexts with different fonts.
Checked: the 14 tests pass together and each passes alone (`--filter` per test, release).

### What a pause does now

`what_the_demo_holds_is_kept_while_the_window_is_collapsed_closed_or_not_shown`: after a collapse,
three frames in which the host does not call at all, and the close button clicked through
`io.AddMouse*Event` with `p_open`: still 5 handles, 0 text builds, the controls where the drags
left them.

### The table is bounded (the lead's condition)

States are kept for the `DEMO_STATES_MAX = 8` contexts the window was last shown in, least
recently used dropped first, so a host that makes contexts in a loop does not grow the demo.
The demo cannot destroy what a dropped state held (it cannot know that the context lives), so a
LIVE context that comes back after eight others starts from the defaults and makes its handles
again beside the old ones, which stay until that context is destroyed.
`a_context_shown_again_after_eight_others_starts_over_and_after_seven_does_not` pins both sides:
5 handles after seven others, 10 after eight. Eight is a number I picked.

### Scripted pixel coordinates

`tests/test_demo.cpp`: one scripted press remained (the slider of copies). The pointer is moved and
`ImGui::GetHoveredID() == GetID("Copies of the document")` (imgui_internal.h, which
tests/test_harden.cpp also includes) is REQUIREd before the button goes down. Shown to bite: with y
moved one row (215 -> 238) the test fails on that line with both IDs printed. The blind drag across
every section is kept, as a function that by design does not care what it hits.

`examples/demo_headless.cpp` (public API only): a script no longer knows positions. `text_drawn()`
finds where ImGui drew a label in the previous frame by matching its glyphs' atlas coordinates in
`ImGui::GetDrawData()`, and the script acts at an offset from it; a text that is not on screen
fails the run, and `expect()` fails it when a press reached nothing (`io.WantTextInput` after a
click on a text field, `ImGui::IsAnyItemActive()` / `IsAnyItemHovered()` otherwise). Shown to bite:
with one label misspelt the run names the missing text and exits 1. Built with
`IMTYPESET_DISABLE_DEMO` the scripts are idle (there is no window to act on).
Limits: the offsets from label to control are still numbers; a control that moves within its
section relative to its label is caught only by `expect()`, which says "something took the press",
not which control. `text_drawn()` depends on ImGui emitting a text as four vertices per visible
glyph, the first carrying (U0, V0) — read in `ImFont::RenderText` of the pinned 1.92.9; if that
changes the scripts fail loudly, not silently.

## The pages

`example_demo_headless DIR`, release, FreeType, 32-bit, final sources. Compared with a second
binary built from the ORIGINAL `src/imtypeset_demo.cpp` and `examples/demo_headless.cpp` (the git
index versions) linked against the same library: **all nine PNGs byte-identical**, so neither the
restructuring nor the label-steered scripts moved a pixel.
LOOKED AT (image opened): all nine. Selection (both drags landed: widget selection bytes [74, 321),
by-hand range [284, 384)), Stress (x1000, scrolled to copies 279-280), Fonts (CMakeLists.txt
refused with its reason), Inspector (caret on line 1), Document, Math (typed formula refused at
`\hat`) were opened from the render of the first design; Paragraph, Faces and sizes, Known limits
from the final one. The six were not reopened after the switch to the serial; they are covered
there by the byte comparison with the original's render.
I wrote no image into `docs/images`.

## Configurations run on the final sources

| configuration | test_demo | example_demo_headless |
|---|---|---|
| release, FreeType, 32-bit | 14 tests / 176 checks ok; each of the 14 alone ok | ok; nine pages |
| ASan+UBSan, FreeType, 32-bit | 14/176 ok (0 of 12 at the old address); quarantine off: 14/176 ok, 12 of 12 at the old address | ok |
| ASan+UBSan, stb, 16-bit ImWchar | 14/176 ok (0 of 12 at the old address); quarantine off: 14/176 ok, 12 of 12 at the old address | ok |
| debug, FreeType, 32-bit | 14/176 ok (11 of 12 at the old address) | ok |
| debug, `-DIMTYPESET_DISABLE_DEMO` (whole build) | 1 test / 3 checks ok | ok (scripts idle) |

Also, on the final sources: `tools/amalgamate.py --out <scratch>` and the generated
`imtypeset_demo.cpp` compiled with `-fsyntax-only` under the project's warning flags, with and
without `IMTYPESET_DISABLE_DEMO` (a compile of that one file, not the `amalgamation` ctest).

NOT run: the full suite in any preset (only `test_demo` and the example; no other source of mine
changed); the presets themselves; `demo_sdl2`; the `amalgamation` ctest; the Python binding; clang
or MSVC; a real display.

## Seen outside the task

- All nine `docs/images/demo-*.png` differ from a fresh render — with the ORIGINAL demo too, so it
  is the library that moved since they were made, not this task. They need re-rendering before a
  release (`docs` or `release-gate`).
- doc-fixes-now's note applies to anyone with an old sidecar: `python/imtypeset` now requires the
  library to export `ImTypeset_GetContextSerial`.
