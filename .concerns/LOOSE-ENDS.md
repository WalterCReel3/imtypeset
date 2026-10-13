# Loose ends after 0.4.1 (written 2026-10-13, at the close of the publishing session)

`imtypeset` 0.4.1 is on PyPI (tag `v0.4.1`, commit `e6160c0`; `main` is one docs commit past it).
What is left, most useful first. Nothing here blocks imscribe declaring the dependency.

## Not started

- **imscribe's open requests**: H22-H28 and H36-H47 in `../imgui-adv-text/HANDOFF-imtypeset.md`
  (characters set as U+FFFD or borrowed; an unbreakable run wider than the measure; a rule under
  every table row; `destroy_context` and the atlas; "no library for this platform"; a caret at a
  code line's end; `edit_error` as a class attribute; `ValueError` naming block and field; values
  taken silently; ragged tables; knowing the ImGui context is gone; white space and HardBreaks;
  metrics without an ImGui context; a double in font metrics; reproducible library bytes;
  estimated document heights). `docs/advtext-swap-status.md` lists them as received.
- **Issue #4**: `test_stress_font`'s kern-table linearity check is a wall-clock bound; it failed
  once on a hosted runner. Count work instead of seconds.

## Never run on a real Mac

- A Retina display: `examples/python/hidpi_check.py` and the five-item checklist in
  `.concerns/hidpi--builder--result.md` (its `--screenshot` needs numpy installed).
- A signed, frozen application: what to report is in `docs/sidecar.md` section 10.
- The self-hosted Python host and the headless demo's scripted shortcuts on macOS (the library
  tests and the wheels have run there; these two have not).
- A real mouse and a GPU anywhere: every window so far was Xvfb or SDL's dummy driver.

## Decisions still the owner's

- Which Macs are the target: Apple Silicon on macOS 14+ is all the imgui-bundle wheels allow (H17).
- Vector output: the four questions at the top of `docs/vector-output.md` (H14).
- Typography: tight justified lines (the line breaker's shrink), the space under a table's header
  rule, widow/orphan control in columns, a common first baseline beside a heading.
- Font sizes round to a device pixel at 2x (one pixel off an ImGui label at non-whole sizes);
  hinted prose as the default (code already is); stb's default for device advances.
- The product copy rules for tables; sharp-s folding in find; a draw-scale parameter.

## Housekeeping

- Branch `publish-0.4.1` on GitHub is identical to `main`: delete when convenient.
- The local tag `v0.4.0` was never pushed; leave it or delete it locally. Tags are never moved.
- TestPyPI is not set up; the workflow's rehearsal input is off by default.
- No wheels for Linux aarch64, musllinux or Windows, though imgui-bundle has them; on Alpine the
  environment marker is true and the install fails (`docs/releasing.md`).
- `build/` holds docker-era directories (`clang-clean*`, `ci-artifacts`, `pypi-release`); four
  docker images (about 1.3 GB) remain. Keep `build/advtext-reference` (the oracle pin) and `.deps/`.
- An application must pin BOTH `imgui-bundle` and the matching `imtypeset` line exactly;
  `pip install -U imtypeset` alone moves imgui-bundle.
- Session notes: the working rules are in `CLAUDE.md`; the task history is `.concerns/BOARD.md`.
