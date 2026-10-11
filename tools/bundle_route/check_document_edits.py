"""imtypeset.Document's edits inside an installed imgui_bundle wheel: what Python
keeps of the document must be what the library holds, after an edit that was
applied, applied in part, or refused.

    IMTYPESET_LIBRARY=<out>/libimtypeset_bundle.so PYTHONPATH=python \
        <python> -P -B tools/bundle_route/check_document_edits.py

Headless: a Dear ImGui context of the wheel's and no window. Prints one line a
check; exit 0 when all pass, 1 otherwise. The same checks run against the
self-hosted library in tests/python/test_binding.py.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT_DIR = os.path.join(REPO, "tests", "data", "fonts")

from imgui_bundle import imgui  # noqa: E402

import imtypeset as ts  # noqa: E402

failed = []


def check(name: str, got, want) -> None:
    ok = got == want
    print("PASS" if ok else "FAIL", name, "" if ok else f": got {got!r}, want {want!r}")
    if not ok:
        failed.append(name)


def raises(call) -> str:
    try:
        call()
    except ts.Error as exc:
        return f"Error {int(exc.code)}"
    return "nothing raised"


def main() -> int:
    imgui.create_context()
    imgui.get_io().backend_flags |= imgui.BackendFlags_.renderer_has_textures
    ts.create_context()
    ts.add_font(ts.Face.Regular, os.path.join(FONT_DIR, "Roboto-Regular.ttf"))
    invalid = f"Error {int(ts.ErrorCode.InvalidArgument)}"
    # One more block than a document holds: the library refuses the edit whole.
    too_many = [ts.Block()] * 4194305

    doc = ts.Document.from_blocks([ts.Block(text="héllo"), ts.Block(text="wörld")])
    doc.layout(400.0)
    check("a refused replace_blocks raises", raises(lambda: doc.replace_blocks(1, 1, too_many)), invalid)
    check("  and Python counts what the library holds", len(doc), doc.block_count)
    check("  and the blocks are the ones before", doc.block_count, 2)
    check("  and positions are still characters",
          doc.range_text(((0, 1), (1, 3)), separator="|"), "éllo|wör")
    check("a refused set_blocks raises", raises(lambda: doc.set_blocks(too_many)), invalid)
    check("  and Python counts what the library holds", len(doc), doc.block_count)
    check("an edit after a refusal is applied", doc.set_blocks([ts.Block(text="naïve"), ts.Block(text="façade")]), True)
    doc.layout(400.0)
    check("  with positions in characters", doc.range_text(((0, 2), (1, 3)), separator="|"), "ïve|faç")

    ruled = ts.Document.from_blocks([ts.Block(ts.BlockKind.Rule)])
    check("a refused append_text raises", raises(lambda: ruled.append_text("é")), invalid)
    check("  and the block's kept size is the library's", ruled._last_size, 0)
    check("  and nothing was kept of the text", ruled._texts, [None])

    doc = ts.Document.from_blocks([ts.Block(text="Tail:")])
    check("append_text says it was applied", doc.append_text(" é é"), True)
    doc.layout(400.0)
    check("  with positions in characters", doc.range_text(((0, 6), (0, 9))), "é é")

    cut = ts.Document.from_blocks([ts.Block(text="before"), ts.Block(text="in no container", container=3), ts.Block(text="après")])
    check("from_blocks keeps what the library cut", cut.edit_error.code, ts.ErrorCode.DescriptionCut)
    check("  and the blocks are set", (len(cut), cut.block_count), (3, 3))
    check("set_blocks answers False for the same", cut.set_blocks([ts.Block(text="x", container=3)]), False)
    check("an edit applied whole clears edit_error",
          (cut.set_blocks([ts.Block(text="x")]), cut.edit_error.code), (True, 0))
    # A table: a block whose text is its cells, appended to a row at a time.
    rows = [["Name", "Unité"], ["masse", "kg"], ["", "vide à gauche"]]
    table = ts.Document.from_blocks(
        [ts.Block(text="avant"),
         ts.Block(ts.BlockKind.Table, rows=rows,
                  flags=ts.BlockFlags.TableHeader | ts.BlockFlags.TableHeld)])
    table.layout(400.0)
    joined = "\n".join("\t".join(row) for row in rows)
    check("a table copies as its cells and rows", table.range_text(((1, 0), (1, len(joined)))), joined)
    geometry = table.table_geometry(1)
    check("  and says its shape", (geometry.row_count, geometry.column_count, geometry.cell_count), (3, 2, 6))
    cell = table.table_cell(table.table_position(1, 1, 1, 1))
    check("  a position names its cell", (cell.row, cell.column, cell.offset), (1, 1, 1))
    before = table.table_column(1, 0)
    check("append_table_rows says it was applied", table.append_table_rows([["a", "b"]]), True)
    table.layout(400.0)
    check("  and the table has the row", table.table_geometry(1).row_count, 4)
    after = table.table_column(1, 0)
    check("  and a held column did not move", (after.min, after.max), (before.min, before.max))
    ts.destroy_context()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
