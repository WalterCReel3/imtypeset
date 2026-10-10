[from the reviewer of math_mlist / mathtext / selection] (filed and condensed by lead; READ from source,
nothing run). No UB, crash or hang was found reachable through include/imtypeset.h in these modules.
OWNERS: `harden-2` takes H1-H9; `tex-web-pass` takes T1-T6.

== harden-2 ==
H1. The parser can emit a list the engine answers TOO_LARGE for: parse() has no size bound,
    mlist_to_hlist stops at MAX_MATH_ITEMS (2^20). mathtext.h and docs/divergences.md (mathtext item 5)
    say this cannot happen. Input: 1,048,577 x `x` -> parse succeeds, then a refusal of kind FONT,
    "the math face could not set it: the math list is too large" — a host branching on FONT blames the
    font. Give it its own kind/reason and bound the SOURCE length in the parser (a stated limit), so
    memory is bounded too: estimated 150-270 bytes of heap per source byte in parse (INFERRED), i.e. a
    100 MB run of `x` is an OOM abort under -fno-exceptions.
H2. (INFERRED, least sure) a formula box wider than MAX_DIMEN is stored as-is by set_formula, against
    the units.h invariant. Input: 25,000 x `\qquad` at 12pt. Trace what the breaker and ship-out do
    with it; clamp or refuse.
H3. Locale: refusal_from formats the LAYOUT reason with "%.2f"; under setlocale(LC_NUMERIC,"de_DE") it
    prints "1,25" and the reason is no longer the reference's. Format without the C locale (integer
    arithmetic on hundredths). Test with setlocale if the locale exists, skip-with-message otherwise.
H4. Reason strings carry raw source characters incl. NUL (as the reference does); a NUL truncates any
    `const char*` consumer (INFERRED, not traced into the public struct). Decide and document:
    escape as the reference's repr() does elsewhere, or expose a length.
H5. Selection on foreign data (detail:: only): SelectionModel::build computes y - height, y + depth,
    x + width unchecked; boundary_in computes x - left on unsorted edges; find_text uses
    std::lower_bound/binary_search on TextData arrays that the contract says are sorted — unsorted
    input gives wrong text, and may abort under _GLIBCXX_DEBUG / libc++ hardening. Validate sortedness
    once at the public boundary (the public API builds these arrays itself — assert it there) and
    clamp coordinates in build().
H6. Nesting depth emitted by parse is 50, not 49 (input: 24 x `\frac` + ` \sin` + 24 x ` x`). The
    bound of 64 still holds; fix the stated argument, the static_assert and the test.
H7. tests/test_mathtext.cpp replay_formulas: the `by_hand` skip count is printed, never asserted, and
    `set` is held only by `> 1500`; about 449 cases could become skips with the test green. Make both
    exact. `arbitrary_sources...` claims more than it tries (sources under 24 pieces).
H8. No test sets LC_NUMERIC; none can catch T1/T2 (every expectation is the reference's).
H9. draw: items D1-D6 of review-draw-para-style--reviewer--findings.md, unless `stress` has done them
    (check docs/hardening.md and its result file first).

== tex-web-pass ==
T1. §755 (tex.web line 14868): TeX appends the italic-correction kern only when there is NO subscript.
    Converter::translate_nucleus (after mlist.py) always appends it and still returns delta: in `f_i`
    the subscript sits delta too far right; in `f_i^2` the subscript is delta too far right and the
    superscript at 2 x delta. The goldens pass because the reference does the same. NOT in
    docs/divergences.md, the header's "WHERE THIS IS NOT tex.web", or the code comment. Whether Latin
    Modern Math's italic letters carry corrections was not read: CHECK (tests/golden/font has the MATH
    italic corrections) and say how visible this is.
T2. §735 (tex.web line 14449): TeX adds one default_rule_thickness to an underline's depth; make_under
    omits it. Input `\underline{x}`: box depth short by one rule thickness. Also unrecorded.
T3. §754: a sublist nucleus is spliced unpacked where TeX hpacks it (mentioned only inside
    make_scripts' comment). §720: a sublist yielding a single unshifted box is wrapped again; TeX
    reuses it. §703/§716: mu is rounded half-to-even where TeX truncates.
T4. Wrong citations: math_mlist.h "two passes, for the reason section 726 gives" is §725;
    size_boundaries "section 726's check_dimensions" is §727. Marginal: is_cramped cites §688 (the
    test is in §758); MathFonts cites §699's fam_fnt (the macro use is §700). 37 citations checked.
T5. MATH_SPACING is §764's 64-character table verbatim: LEAD RULING — it is data that must be exactly
    that string to be correct; keep it, cite §764 at the definition, list it in THIRD-PARTY.md under
    tex.web as the one verbatim datum.
T6. LEAD RULING on T1 and T2: behaviour stays with the Python oracle (D23), so the port does NOT
    change; both are recorded in docs/divergences.md as "the reference departs from Appendix G here,
    and the port follows the reference", stated at the code, with the tex.web section and line — and
    reported to the user as reference defects they may want fixed in both places.
