# issue4 (builder): test_stress_font counts work, not seconds

## What changed

- `tests/test_stress_font.cpp`: global `operator new`/`delete` replaced for this one
  binary (the pattern `tests/test_selection.cpp` already uses), counting allocations and
  bytes requested. `CountAllocations` / `AllocationCount` take the difference between two
  points. The deletes call `free` through an out-of-line helper; without it GCC -O3 raises
  `-Wmismatched-new-delete` (seen in the release build, fixed there).
- `src/opentype.h`, `src/opentype.cpp`: `OpenTypeFont::outlines_run()`, the number of
  outlines `bounds()` has interpreted since the font was loaded. It is one `size_t`
  increment next to each outline run and nothing when the box is already known. No
  behaviour change. `reset()` clears it.
- `docs/decisions.md`: D86 appended.

## The seconds-based bounds that were in the file, and what each became

| test | before | now |
|---|---|---|
| n3, 65,535 one-pair subtables | `seconds < 5`, `seconds < 8 * quarter_seconds` (the flake) | allocations(65,535) − allocations(16,384) ≤ 32; bytes − bytes(no kern) ≤ 16 × kern table bytes |
| n3, 16,384 subtables, one pair | `seconds < 5` | bytes over the no-kern load ≤ 16 × table; allocations over it ≤ 64 |
| n1, format 13 over the whole code space | load/register/build each `< 10 s` | each phase allocates < 1 MiB (one entry per code would be 4.25 MB) |
| n1, 100,000 format 12 groups | `< 5 s` | load allocates < 64 KiB |
| n2, 256 DICTs sharing one Subrs | `load < 5 s` | load bytes ≤ 2 × file size (256 copies would be 64 MB) |
| n5, CFF stack bomb | `ink box < 5 s` | dropped. The existing "malformed or unsupported" warning check is the deterministic witness: shown below to fail with no budget |
| s5, remembered ink box | 1000 calls `< first + 0.05 s` (relative, the same kind of flake) | `outlines_run()` is 0 before, 1 after the first call, and still 1 after 1,000 more; the boxes are equal |
| n4, post names | `< 5 s`, `register < 10 s` | **kept**, with a comment saying why. The quadratic version does 300 M lookups of SSO strings and allocates nothing extra, so no count can tell it apart. Measured under ASan: 0.17 s and 0.18 s |

Every time is still printed. None is asserted except n4's two ceilings.

## Why it is deterministic

The counts depend on the input, the library and the standard library's growth policy,
not on the machine's load. Measured: debug, release and ASan+UBSan gave identical counts
to the byte (n3: 60 / 54 / 8 allocations; 3,015,062 / 754,070 / 410 bytes; n2: 20
allocations, 264,181 bytes; n1: 410 / 3,481 / 1,876 bytes). The bounds leave headroom
for a vector growth factor of 1.5 (MSVC) as well as libstdc++'s 2. Today n3 has 6
allocations of slack used out of 32, and 2.3× the table out of 16×.

## Proof that the checks still fail on the quadratic version

I made a scratch copy of the tree under `build/issue4-quad-src`, so `src/` itself was
never modified. In that copy I put `pairs.reserve(pairs.size() + count);` back in
`read_legacy_kern`, built it separately (debug) and ran n3. All four count checks
failed:

    65,535: 65,578 allocations, 25,770,852,770 bytes, 0.728 s
    16,384: 16,423 allocations, 1,611,071,906 bytes
    one pair x 16,384: 16,395 allocations, 1,610,809,778 bytes
    FAILED: 4 failed check(s)  (lines: allocations diff <= 32; bytes <= 16x, twice; allocations <= 64)

In the same scratch copy I also removed `bounds()`'s early return: s5 fails with
`outlines_run()` reading 1001 against 1. With `CFF_WORK_BUDGET = 0x7FFFFFFF`, n5's
warning check fails (the ink box took 2.49 s). I deleted the scratch copy afterwards. The
real tree never had the mutation.

## What ran (all under the memory guard, -j4, private build dirs, now removed)

- debug: `test_stress_font` complete, 22 tests, 0 failed (run twice, before and after the
  final comment edit).
- release (-O3): complete, 22 tests, 0 failed.
- ASan+UBSan (`hard_rss_limit_mb=3000`): complete, 22 tests, 0 failed, no sanitizer report.

## Row for the lead's status file

`issue4` | done | test_stress_font: n1/n2/n3 now held to allocation and byte counts and s5 to `OpenTypeFont::outlines_run()`; n5's time bound dropped (its warning is the witness); n4 keeps 5 s/10 s ceilings (no count separates its quadratic case). Shown to fail with W-D2's exact reserve put back (65,578 vs 54 allocations, 25.8 GB). Debug, release and ASan pass with identical counts. D86. Touches `src/opentype.{h,cpp}` (counter only).

## Not verified

- Any other preset: stb, wchar16, amalgamation, exceptions, macOS/Clang, MSVC. On Clang
  and macOS, the replaced `operator new` and the `__attribute__((noinline))` helper have
  not been compiled.
- Other tests that include `src/opentype.h` were not rebuilt in my narrowed build. The
  change only adds a member and an accessor.
- n1 and n2 were not shown to fail with their own regressions put back. Only n3, s5 and
  n5 were.
- A standard library other than libstdc++. The bounds are argued, not measured, for
  growth factor 1.5.
