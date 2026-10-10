# Lead rulings on the public-API review (for task `api-revise`)

The review is `.concerns/review-public-api--reviewer--findings-full.md` (five posts, read from source,
nothing run). The header has not been released, so everything below is changed NOW; after release each
would be a break. Reproduce a claimed defect with a failing test before fixing it; if one does not
reproduce, say so and leave the code.

R1. Widget identity. `Text()` must not key its layout on the text POINTER. Key it on the ImGui ID stack
    plus a hash of the CONTENT (and offer an entry taking an explicit `str_id`, the ImGui way, for text
    that changes every frame). Remove `ImTypesetTextFlags_NoTextCompare` and anything else that
    publishes pointer identity. Cap the widget cache (entries, and bytes), evict least-recently-used,
    and expose the cap in the style or context. Tests: the same string in a fresh temporary each frame
    lays out once; a stack buffer reused in a loop of N rows lays out N times total, not N per frame;
    inserting a row does not re-lay-out the rows below it.
R2. Counters. No lifetime `int` accumulator can overflow: 64-bit, or per-frame and reset. Stats is a
    debug facility; say so in the header.
R3. Offsets. THE LEAD WITHDRAWS the earlier ruling that exposed shipped indices. The public API speaks
    SOURCE TEXT BYTE OFFSETS everywhere (hit-test results, selection ranges, rectangles for a range,
    copy), with helpers converting byte offset <-> codepoint offset for bindings whose strings are not
    UTF-8 byte arrays. Shipped indices stay internal. A selection expressed in byte offsets survives a
    reflow by construction; test that it does.
R4. Context and build check. Follow Dear ImGui: `CreateContext()` takes no build-check parameters; a
    separate `IMTYPESET_CHECKVERSION()` macro calls `DebugCheckVersionAndDataLayout(version, sizeof...)`
    covering EVERY struct a host passes (including `ImTypesetRun`) and `sizeof(ImWchar)`, so adding a
    check never changes a signature a binding wrapped. `CreateContext` makes the new context current
    only if none is (as `ImGui::CreateContext` does). Every public struct a host fills is
    zero-initialisable to defaults OR has an init function, and the header says which fields may be
    appended in a minor version.
R5. Errors. One model: every call that can fail returns a status; the reason is retrievable from the
    object it concerns (context or paragraph) and from a context-less `GetLastError()` when no context
    could be made; errors are cleared by the next successful call of the same kind (no stale
    `LayoutFailed`); the widgets record their failures (text too long, layout failed, no-font fallback,
    refused formula) where a host can read them that frame.
R6. Crashes and silent garbage, each with a test: drawing after the ImGui context is gone or with a
    different ImGui context current is a refused call with a reason, not a null dereference; a backend
    without `ImGuiBackendFlags_RendererHasTextures` is refused at the first frame with a reason; a font
    the host removed from the atlas is detected (atlas generation or font lookup) rather than
    dereferenced, and there is a `RemoveFont`/replace entry; font files open correctly from UTF-8 paths
    on Windows (`_wfopen` behind `#ifdef _WIN32`; not compiled here — say so).
R7. Layout cost. Total fit gets its work budget in `stress` (already ruled); here: a width change must
    not re-break paragraphs that are not visible when their previous height can stand in until they are
    (re-layout lazily on becoming visible, and document the scrollbar consequence), or justify with
    evidence why not.
R8. Widget semantics. Per-call wrap width and alignment for the widget entry; the item is as wide as
    the text block it reports, not always the full available width; an explicit colour respects
    `style.Alpha` (so `BeginDisabled()` dims it). The text-baseline offset after a framed widget cannot
    be set through `imgui.h` alone: document it and show `AlignTextToFramePadding()` in the header
    comment; do NOT include `imgui_internal.h` to fix it.
R9. Names and shape. No function with ImGui's printf name (`Text`) and an unformatted signature: use
    ImGui's own split (`TextUnformatted(text, text_end)`; a printf-style `Text(fmt, ...)` only if it
    really formats, with IM_FMTARGS). `TextJustified` must differ from the default entry or go.
    First-fit / total-fit is one enum, not two bits. Nothing inert stays public except `face_index`
    (task `font-wild` gives it meaning). `DestroyParagraph` uses the paragraph's own context and is not
    linear in paragraph count.
R10. Add, because a first user needs them and they cost little now: a one-shot measure without a
    handle (the `CalcTextSize` analogue: size for a text at a wrap width); line and caret geometry for
    a paragraph (line count, each line's rectangle and baseline, the caret rectangle at a byte offset);
    a selectable-text helper that draws the selection highlight and puts the copy on the clipboard
    through `ImGui::SetClipboardText`. NOT added, and listed in the README as known gaps: fallback
    faces for CJK/emoji/icons (the seven fixed roles stay; `MergeMode` stays refused, D15/D27), display
    math beyond what `api-math` shipped, background-thread layout.
R11. Tests the review found weaker than claimed: the steady-state test asserts on the SECOND frame, for
    several paragraphs, over the whole frame; culling is shown sub-linear by scaling the document
    (x1, x10, x100) with a fixed window and bounding the work counter; the zero-cost claim for a
    clipped widget holds on the DEFAULT path or is restated honestly; allocations are counted on the
    paragraph path, with a selection, through `DrawParagraphToBuffers`, and across a resize.

Everything that changes a declaration is recorded in `docs/decisions.md` (append) with the reason; every
deliberate departure from the reference in `docs/divergences.md`. Update every caller: tests/test_api.cpp,
tests/test_api_math.cpp, tests/test_header_pod.cpp, tests/test_stress_text.cpp, examples/*.cpp,
bench/*.cpp. After the change, run litgen-style sanity by hand: list every declaration and state, for
each, the one thing a binding generator needs (ownership, nullability, buffer size parameter) — and put
that list in the result report; no generator is installed here, so this is a reading, not a run.

R12. Math switches (from `.concerns/api-math--builder-14--result.md`, decisions 1 and 3). ONE switch:
    the per-text flag `ImTypesetTextFlags_Math`. Remove the context-level `EnableMath()`; with the flag
    set and no math face registered, the formula is refused with a reason (that is the off-by-default
    the reference has: nothing happens unless the host asks per text). Update the one assertion in
    tests/test_api.cpp that pinned the two-switch behaviour. Display math KEEPS the builder's rule — a
    text that is wholly a `$$...$$` block is set centred in display style under the same flag — because
    it is the reference's convention and needs no second flag; say so in the header.
    `AppendParagraphSelectionText` allocating about 14 times per call is acceptable (copy is not a
    per-frame path); state it in the header rather than chase it.
