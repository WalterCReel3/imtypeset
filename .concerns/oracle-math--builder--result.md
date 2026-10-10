[from builder] oracle-math done: 6 golden files (13.7 MB), and port hazards for mathtext / math-mlist / api-math that no spec names
Exists: tools/oracle/dump_math.py (+ --verify), tools/oracle/FORMAT-math.md, tests/golden/math/{style,unicode,mathtext,mlist,formulas,inline}.golden. 8,531 cases; two runs byte-identical (sha256 compared); --verify replays 10,701 checks ok and fails when I corrupt one recorded float (tried). All 457k records read cleanly through tests/golden.h; every integer fits int64.

Things downstream builders need that their specs do not say:

1. math-mlist can be tested with NO font reader. Every font question the reference asks during a conversion is recorded per case (q_advance/q_bbox/q_italic/q_variants/q_kern/q_has/q_space, params in a per-file table). --verify replays all 4,613 conversions from those records alone and gets identical nodes/boxes/commands/refusals. Finding: mlist never asks a face for ascent, descent, x_height, em or size. So a divergence can be pinned on mlist vs on the font reader separately.

2. mathtext: refusal reason strings embed Python repr() of a character ("the character '\x00' (U+0000) ...", '​', '\U0010ffff') and typeset's layout reason embeds f"{needed/largest:.2f}". "reason strings must equal the reference's" therefore means reimplementing repr()'s printable/escape rule. FORMAT-math.md section 5 states exactly which forms can occur.

3. mathtext/para/api-math need Unicode tables Python gets for free: str.isspace (blank-skipping; U+00A0 etc.), str.isprintable (\text{}; 700 ranges), str.isalnum (the inline-$ guards), str.isupper (space factor). tests/golden/math/unicode.golden has each as ranges over 0..0x10FFFF, plus categories Cc/Cf/Mn/Me for para's stand-in rule. Unicode 14.0.0 (the reference's Python 3.11); a table generated from a newer UCD will differ and that is then a version difference, not a bug.

4. Refusal `position` is in CODEPOINTS in the reference. C++ hosts want bytes. Goldens carry both (position, byte_position); public-api/mathtext must pick and say which.

5. math-mlist: DELIMITER_SHORTFALL has to be a parameter, not a constant, to replay the round-number boundary cases (the reference's tests monkeypatch it to 9000 for the chained fake). Each mlist case records `delimiter_shortfall` as an input.

6. Not every engine failure is a refusal: `accent` raises NotImplementedError and a malformed field raises TypeError; neither is a MathLayoutError, so typeset() only catches them in its catch-all as kind `font`. mlist.golden has 8 such cases (slugs not-implemented, malformed-field); the port needs a defined, non-crashing outcome (D17). mathtext never emits either.

7. api-math: where a `$` opens a formula is a markdown-it inline rule (advtext/markdown._math_inline), which D1 does not port. inline.golden gives the segmentation for 59 plain-text paragraphs + 12 `$$` blocks, the FormulaSpans, and the resulting paragraph as a FORMAT.md `linebreak` case. NOT covered because they are markdown's: `\$` escapes, code spans, emphasis/links around a formula, more_may_follow. api-math has to state what the native scanner does with a backslash before a dollar.

8. style: parse_font_key uses Python int(): accepts "+5", " 5", "1_000" and Arabic-Indic digits. Recorded as the reference behaves; with interned keys (D21) the port may never parse one, in which case this is moot - say so rather than port it.

Coupling to know about: dump_math.py imports dump_core.py (Writer, emit_*, read_*, paragraph_case, verify_case, Rng). If oracle-core's functions change signature, dump_math breaks; if they change output, math goldens change.

Deviations / my additions: (a) the `chained` fake has italic corrections for 'f' and 'V' that the reference's test fake does not - without them no fake case reaches the italic kern or delta; documented. (b) the reference's test tables (REFUSALS, GALLERY, FUZZ_PIECES) are restated in the script rather than imported from its tests/. (c) hostile-size sources (50,000 repeats) are capped at 400 repeats here; the real sizes belong to the stress task.

Least sure about: whether 13.7 MB is acceptable in the repo (tests/golden is now ~26 MB total). The q_* records are about a third of it; dropping them loses item 1. Also not verified by me: nothing C++ reads these yet beyond tokenising - tests/math_golden.h (math-mlist) is the first real consumer and may find the record layout awkward.

Unrelated, noticed: `git status` in ../imgui-adv-text shows uncommitted modifications to docs/cpp-extension.md, docs/guide/README.md, tests/test_docs.py and an untracked postmortem doc. Not ours (the sandbox cannot write there) - but the spec everyone is reading is the working-tree version, not HEAD.
