// Reader for the `table` case of tools/oracle/FORMAT-tables.md: its INPUT (the
// theme, the table's style, its policy, and each cell as the node lists the
// reference derived), and writers that put what the port produced in the
// oracle's spelling, so that a difference is shown as two lines. The lexical
// layer is tests/golden.h's, the node, spec, measure, packed, break and command
// records are tests/core_golden.h's, and the metrics records and the writers
// for a broken paragraph and for commands are tests/blocks_golden.h's.

#pragma once

#include "blocks_golden.h"

#include "align.h"

#include <cinttypes>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tables_golden
{

using namespace ImTypeset::detail;

inline constexpr const char* const COLUMN_ALIGN_TOKENS[] = {"left", "center", "right"};
// In SolveRegime's order.
inline constexpr const char* const SOLVE_REGIME_TOKENS[] = {
    "given", "fractions", "apportioned", "unscaled", "squeezed", "floors", "rigid"};

inline const char* token(SolveRegime regime)
{
    return core_golden::token_at(SOLVE_REGIME_TOKENS, static_cast<unsigned>(regime));
}

// One cell: its material, and the text of the reference's `unshrinkable`.
struct CellIn {
    std::vector<Node> material;
    std::vector<std::string> unshrinkable;
    uint32_t events = 0;
    uint32_t atoms = 0;
};

// The INPUT of one `table` case.
struct TableIn {
    double px_per_inch = 0.0;
    std::string spacing;
    uint32_t columns = 0;
    uint32_t row_count = 0;
    bool has_header = false;
    Scaled space_above = 0;
    Scaled space_below = 0;
    std::string font;
    TableStyle style;
    std::vector<ColumnAlign> aligns;
    WidthPolicy policy = WidthPolicy::APPORTIONED;
    std::vector<Scaled> widths;
    std::vector<double> fractions;
    std::vector<std::vector<CellIn>> rows;

    ColumnPolicy column_policy() const
    {
        ColumnPolicy out;
        out.kind = policy;
        out.widths = widths.data();
        out.fractions = fractions.data();
        out.count = policy == WidthPolicy::WIDTHS      ? widths.size()
                    : policy == WidthPolicy::FRACTIONS ? fractions.size()
                                                       : 0;
        return out;
    }
};

// Reads `theme`, `faces` and the metrics records: everything before `table`.
// The `measure` records are answered to the caller; the others are what the
// reference's composition asked while it built the cells, which a replay from
// the recorded node lists does not ask again.
inline core_golden::MeasureTable read_theme(core_golden::Reader& reader,
                                            core_golden::Context& context, TableIn& out)
{
    golden::Cursor theme(reader.take("theme"));
    out.px_per_inch = theme.f64();
    out.spacing = std::string(theme.raw());
    out.style.cell_padding = theme.i64();
    out.style.rule_thickness = theme.i64();
    theme.expect_done();
    reader.take("faces");
    blocks_golden::Recorded asked;
    asked.read(reader, context);
    return core_golden::read_measure(reader, context);
}

// Reads `table` ... `endtable`.
inline void read_table(core_golden::Reader& reader, core_golden::Context& context, TableIn& out)
{
    const golden::Record& open = reader.take("table");
    golden::Cursor cursor(open);
    out.columns = cursor.u32();
    out.row_count = cursor.u32();
    out.has_header = cursor.boolean();
    out.style.left_indent = cursor.i64();
    out.space_above = cursor.i64();
    out.space_below = cursor.i64();
    out.font = cursor.str();
    cursor.expect_done();

    const core_golden::Skip skip = core_golden::read_skip(reader);
    out.style.interline.baselineskip = skip.baselineskip;
    out.style.interline.lineskip = skip.lineskip;
    out.style.interline.lineskiplimit = skip.lineskiplimit;
    golden::Cursor strut(reader.take("strut"));
    out.style.strut.height = strut.i64();
    out.style.strut.depth = strut.i64();
    strut.expect_done();

    golden::Cursor aligns(reader.take("aligns"));
    const uint32_t align_count = aligns.u32();
    for (uint32_t i = 0; i < align_count && !aligns.done(); ++i)
        out.aligns.push_back(static_cast<ColumnAlign>(
            core_golden::token_index(aligns.raw(), COLUMN_ALIGN_TOKENS, "ColumnAlign")));
    aligns.expect_done();

    golden::Cursor policy(reader.take("policy"));
    const std::string kind(policy.raw());
    if (kind == "apportioned") {
        out.policy = WidthPolicy::APPORTIONED;
    } else if (kind == "widths") {
        out.policy = WidthPolicy::WIDTHS;
        const uint32_t count = policy.u32();
        for (uint32_t i = 0; i < count && !policy.done(); ++i)
            out.widths.push_back(policy.i64());
    } else if (kind == "fractions") {
        out.policy = WidthPolicy::FRACTIONS;
        const uint32_t count = policy.u32();
        for (uint32_t i = 0; i < count && !policy.done(); ++i)
            out.fractions.push_back(policy.f64());
    } else {
        check::fail(open.file, open.line, "the policy is not a width policy");
    }
    policy.expect_done();

    while (!reader.done() && reader.next_is("row")) {
        golden::Cursor row(reader.take("row"));
        if (row.u32() != out.rows.size())
            check::fail(open.file, open.line, "rows are not numbered in order");
        const uint32_t cell_count = row.u32();
        row.expect_done();
        std::vector<CellIn> cells;
        for (uint32_t c = 0; c < cell_count && !reader.done(); ++c) {
            golden::Cursor cell(reader.take("cell"));
            CellIn in;
            if (cell.u32() != out.rows.size() || cell.u32() != c)
                check::fail(open.file, open.line, "cells are not numbered in order");
            in.events = cell.u32();
            cell.expect_done();
            for (uint32_t e = 0; e < in.events && !reader.done(); ++e) {
                if (reader.next_is("atom")) {
                    reader.take();
                    core_golden::read_node(reader, context);
                    ++in.atoms;
                } else {
                    reader.take("run");
                }
            }
            std::string name;
            in.material = core_golden::read_nodes(reader, context, &name);
            if (name != "material")
                check::fail(open.file, open.line, "expected `nodes material`");
            in.unshrinkable = blocks_golden::read_node_lines(reader, context, "unshrinkable");
            reader.take("endcell");
            cells.push_back(std::move(in));
        }
        reader.take("endrow");
        out.rows.push_back(std::move(cells));
    }
    reader.take("endtable");
    if (out.rows.size() != out.row_count)
        check::fail(open.file, open.line, "`table` does not count its rows");
}

// --- Writing what the port produced as the oracle writes it -----------------------------

inline std::string counted(std::string_view tag, const Scaled* values, size_t count)
{
    std::string out = check::format("%.*s %zu", static_cast<int>(tag.size()), tag.data(), count);
    for (size_t i = 0; i < count; ++i)
        out += check::format(" %" PRId64, values[i]);
    return out;
}

inline std::string counted(std::string_view tag, const std::vector<Scaled>& values)
{
    return counted(tag, values.data(), values.size());
}

// The `solve` ... `endsolve` group. `widest` and `narrowest` are what the
// solve was given; a null `narrowest` writes an empty record, which no golden
// holds.
inline std::vector<std::string> written(const ColumnSolve& solve, const Scaled* widest,
                                        const Scaled* narrowest, size_t columns)
{
    std::vector<std::string> out;
    out.push_back(
        check::format("solve %s %" PRId64 " %zu", token(solve.regime), solve.available, columns));
    const size_t floors = narrowest != nullptr ? columns : 0;
    switch (solve.regime) {
    case SolveRegime::GIVEN:
        break;
    case SolveRegime::FRACTIONS:
        out.push_back("total " + core_golden::hex_float(solve.share_total));
        out.push_back(counted("rounded", solve.floored));
        out.push_back(check::format("last %" PRId64, solve.last));
        out.push_back(check::format("borrowed %" PRId64, solve.borrowed));
        break;
    case SolveRegime::APPORTIONED:
        out.push_back(counted("widest", widest, columns));
        out.push_back(check::format("total %" PRId64, solve.total));
        out.push_back(counted("floored", solve.floored));
        break;
    case SolveRegime::UNSCALED:
        out.push_back(counted("widest", widest, columns));
        out.push_back(check::format("total %" PRId64, solve.total));
        break;
    case SolveRegime::FLOORS:
    case SolveRegime::RIGID:
    case SolveRegime::SQUEEZED:
        out.push_back(counted("widest", widest, columns));
        out.push_back(counted("narrowest", narrowest, floors));
        out.push_back(check::format("slack %" PRId64, solve.slack));
        if (solve.regime != SolveRegime::FLOORS) {
            out.push_back(counted("give", solve.give));
            out.push_back(check::format("total %" PRId64, solve.total));
        }
        if (solve.regime == SolveRegime::SQUEEZED)
            out.push_back(counted("floored", solve.floored));
        break;
    }
    out.push_back(counted("columns", solve.columns));
    out.push_back("endsolve");
    return out;
}

// A box record written from a header that is not a node of its own: `box`'s
// fields and the contents it names.
inline std::vector<std::string> box_lines(const core_golden::Context& context, const Box& box)
{
    std::vector<std::string> out;
    const NodeView contents = context.arena.view(box.contents);
    out.push_back(check::format("box %s %s %zu", core_golden::token(box.kind),
                                blocks_golden::box_header(box).c_str(), contents.size()));
    for (const Node& child : contents)
        core_golden::write_node(context, child, out);
    out.push_back("endbox");
    return out;
}

// `<tag> <width> <height> <depth>` and, with `shift`, the shift_amount.
inline std::string dims(std::string_view tag, const Box& box, bool shift)
{
    std::string out =
        check::format("%.*s %" PRId64 " %" PRId64 " %" PRId64, static_cast<int>(tag.size()),
                      tag.data(), box.width, box.height, box.depth);
    if (shift)
        out += check::format(" %" PRId64, box.shift_amount);
    return out;
}

// One box of a stacking as the `stack` group has it: `append`, the interline
// glue when there is some, and `ref`.
inline void write_stacked(const core_golden::Context& context, const TableBox& box,
                          const std::vector<Node>& vlist, std::vector<std::string>& out)
{
    out.push_back(check::format("append %" PRId64 " %" PRId64 " %" PRId64, box.prev_depth_in,
                                box.prev_depth_out, box.prev_depth_carried));
    if (box.has_glue && box.vlist_index >= 1 && box.vlist_index < vlist.size())
        core_golden::write_node(context, vlist[box.vlist_index - 1], out);
    if (box.row < 0)
        out.push_back("ref headrule");
    else
        out.push_back(check::format("ref row %" PRId64, box.row));
}

inline std::vector<std::string> written(const core_golden::Context& context,
                                        const std::vector<TableBox>& boxes,
                                        const std::vector<Node>& vlist)
{
    std::vector<std::string> out;
    out.push_back(check::format("stack %zu", boxes.size()));
    for (const TableBox& box : boxes)
        write_stacked(context, box, vlist, out);
    out.push_back("endstack");
    return out;
}

} // namespace tables_golden
