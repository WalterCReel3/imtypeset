// Tables: src/align against the reference's table layout.
//
// The goldens of tests/golden/tables that hold `table` cases (widths, shapes,
// demo; tools/oracle/FORMAT-tables.md sections 1 to 3) are replayed record by
// record from the node lists and the metrics they record: every cell's natural
// width and longest word, every solve with the values it went through, every
// row with its box (and, for a wrapped row, every cell's break, lines and
// \vtop), the head rule, the stacking with the prev_depth each box carried,
// the table's box and what it ships. Integers equal, doubles to the last bit.
// Each layout is produced twice, by the steps one at a time and by
// Aligner::fin_align, and both are held to the records.
//
// Below that, tables the reference cannot construct or was never asked: bad
// policies, hostile node lists, lengths at the ends of a Scaled, and tables
// large enough that anything quadratic would show.

#include "tables_golden.h"

#include "align.h"
#include "paragraph.h"
#include "shipout.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace ImTypeset::detail;

namespace
{

struct Tally {
    int cases = 0;
    int skipped = 0;
    int layouts = 0;
    int empty_tables = 0;
    int rows_fit = 0;
    int rows_wrapped = 0;
    int regimes[7] = {0, 0, 0, 0, 0, 0, 0};
    int cells = 0;
    int words = 0;
    int word_lines = 0;
    int wrapped_cells = 0;
    int cell_lines = 0;
    int head_rules = 0;
    int stacked = 0;
    int resumed_differs = 0;
    int atoms = 0;
    size_t cmds = 0;
    size_t mapped_nodes = 0;
    size_t added_items = 0;
};

LineBreakParams word_params()
{
    LineBreakParams params = prose_breaking();
    params.first_fit = true;
    params.left_skip = GlueSpec();
    params.right_skip = GlueSpec();
    return params;
}

// Reads a `break` group and holds the paragraph it records against `broken`,
// which was broken at a uniform `measure` under `params` from `list`.
void check_break(core_golden::Reader& reader, core_golden::Context& context,
                 std::string_view list_name, Scaled measure, const LineBreakParams& params,
                 const Paragraph& broken)
{
    const golden::Record& group = reader.take("break");
    CHECK_EQ(std::string(group.raw(1)), std::string(list_name));
    const core_golden::Shape shape = core_golden::read_shape(reader);
    REQUIRE_EQ(shape.lines.size(), size_t(1));
    CHECK_EQ(shape.lines[0].width, measure);
    CHECK_EQ(shape.lines[0].indent, Scaled(0));
    CHECK(blocks_golden::same_params(core_golden::read_params(reader), params));
    const size_t first = reader.position();
    const core_golden::ParagraphResult expected = core_golden::read_paragraph(reader);
    for (int64_t i = 0; i < expected.line_count && !reader.done(); ++i)
        core_golden::read_packed(reader, context);
    CHECK_LINES_EQ(reader.lines(first, reader.position()), blocks_golden::written(context, broken),
                   "the broken cell");
    reader.take("endbreak");
}

// The records from the reader's position to one past the node record there.
std::vector<std::string> take_node_lines(core_golden::Reader& reader, core_golden::Context& context)
{
    const size_t first = reader.position();
    core_golden::read_node(reader, context);
    return reader.lines(first, reader.position());
}

// A table's cells as fin_align takes them.
struct Flat {
    std::vector<NodeView> cells;
    std::vector<uint32_t> row_first;

    explicit Flat(const tables_golden::TableIn& table)
    {
        row_first.push_back(0);
        for (const std::vector<tables_golden::CellIn>& row : table.rows) {
            for (const tables_golden::CellIn& cell : row)
                cells.push_back(NodeView(cell.material));
            row_first.push_back(static_cast<uint32_t>(cells.size()));
        }
    }

    TableInput input(const tables_golden::TableIn& table) const
    {
        TableInput in;
        in.cells = cells.data();
        in.row_first = row_first.data();
        in.rows = table.rows.size();
        in.has_header = table.has_header;
        in.aligns = table.aligns.data();
        in.align_count = table.aligns.size();
        in.policy = table.column_policy();
        in.style = table.style;
        return in;
    }
};

void replay_case(const golden::File& file, const core_golden::Case& c, Tally& tally)
{
    core_golden::Context context;
    core_golden::Reader reader(file, c);
    reader.take("source");

    tables_golden::TableIn table;
    const core_golden::MeasureTable measured = tables_golden::read_theme(reader, context, table);
    blocks_golden::TableMeasure table_measure(measured);
    const CharMeasure measure = table_measure.measure();
    tables_golden::read_table(reader, context, table);
    // What the reference's theme makes of the style: the strut that fills one
    // leading.
    {
        const Strut filling = Strut::filling(table.style.interline.baselineskip.width);
        CHECK_EQ(filling.height, table.style.strut.height);
        CHECK_EQ(filling.depth, table.style.strut.depth);
    }

    const size_t rows = table.rows.size();
    const size_t columns = table.columns;
    const Scaled padding = table.style.cell_padding;
    const LineBreakParams params = prose_breaking();
    Aligner aligner;

    // --- INPUT: `unshrinkable` is what src/paragraph makes of `material`.
    for (size_t r = 0; r < rows; ++r) {
        for (size_t col = 0; col < table.rows[r].size(); ++col) {
            CHECK_CONTEXT("cell %zu.%zu", r, col);
            const tables_golden::CellIn& cell = table.rows[r][col];
            std::vector<Node> shrunk;
            unshrinkable(cell.material, shrunk);
            std::vector<std::string> actual;
            core_golden::write_nodes(context, "unshrinkable", shrunk, actual);
            CHECK_LINES_EQ(cell.unshrinkable, actual, "unshrinkable");
            tally.atoms += static_cast<int>(cell.atoms);
            ++tally.cells;
        }
    }

    // --- EXPECTED, whatever the measure: `limits`.
    std::vector<std::vector<Scaled>> natural(rows);
    std::vector<Scaled> widest;
    std::vector<Scaled> narrowest;
    reader.take("limits");
    if (rows != 0) {
        for (size_t r = 0; r < rows; ++r) {
            CHECK_CONTEXT("natural, row %zu", r);
            PackError error = PackError::NONE;
            for (const tables_golden::CellIn& cell : table.rows[r]) {
                const Scaled w =
                    natural_width(context.arena, cell.material, padding, measure, &error);
                natural[r].push_back(w);
                CHECK(fin_col(widest, natural[r].size() - 1, w));
            }
            CHECK(error == PackError::NONE);
            const size_t at = reader.position();
            reader.take("natural");
            CHECK_EQ(reader.line_text(at),
                     tables_golden::counted(check::format("natural %zu", r), natural[r]));
        }
        widest.resize(columns, 0);
        {
            const size_t at = reader.position();
            reader.take("widest");
            CHECK_EQ(reader.line_text(at), tables_golden::counted("widest", widest));
        }
        const LineBreakParams expected_params = word_params();
        for (size_t r = 0; r < rows; ++r) {
            for (size_t col = 0; col < table.rows[r].size(); ++col) {
                CHECK_CONTEXT("word %zu.%zu", r, col);
                const tables_golden::CellIn& cell = table.rows[r][col];
                // Kept, to be compared with the reference's lines; the same
                // call that drops them must answer the same.
                const Scaled dropped =
                    aligner.longest_word(context.arena, cell.material, params, measure);
                const size_t boxes_before = context.arena.box_count();
                const Scaled word =
                    aligner.longest_word(context.arena, cell.material, params, measure, true);
                CHECK_EQ(dropped, word);
                golden::Cursor record(reader.take("word"));
                CHECK_EQ(record.u32(), static_cast<uint32_t>(r));
                CHECK_EQ(record.u32(), static_cast<uint32_t>(col));
                CHECK_EQ(record.i64(), word);
                record.expect_done();
                CHECK(fin_col(narrowest, col, word + 2 * padding));
                ++tally.words;
                if (cell.material.empty()) {
                    CHECK(!reader.next_is("break"));
                    CHECK_EQ(word, Scaled(0));
                    CHECK(aligner.word_lines().empty());
                    CHECK_EQ(context.arena.box_count(), boxes_before);
                    continue;
                }
                check_break(reader, context, "material", UNITY, expected_params, aligner.broken());
                const size_t at = reader.position();
                reader.take("wordlines");
                CHECK_EQ(reader.line_text(at),
                         tables_golden::counted("wordlines", aligner.word_lines()));
                tally.word_lines += static_cast<int>(aligner.word_lines().size());
            }
        }
        narrowest.resize(columns, 0);
        const size_t at = reader.position();
        reader.take("floor");
        CHECK_EQ(reader.line_text(at), tables_golden::counted("floor", narrowest));
    }
    reader.take("endlimits");

    const Flat flat(table);
    Aligned whole;
    RowTrace trace;
    std::vector<DrawCmd> cmds;

    // --- EXPECTED: one layout per width.
    while (reader.next_is("layout")) {
        const golden::Record& open = reader.take("layout");
        const std::string run = open.str(0);
        const Scaled width = open.i64(1);
        CHECK_CONTEXT("layout %s (line %d)", run.c_str(), open.line);

        // The whole table in one call, to hold beside the steps.
        CHECK(aligner.fin_align(context.arena, flat.input(table), width, params, measure, whole));
        CHECK_EQ(whole.natural.size(), flat.cells.size());

        // The widths.
        ColumnSolve solve;
        if (rows != 0) {
            const ColumnPolicy policy = table.column_policy();
            const bool settled = column_widths(policy, widest.data(), nullptr, columns, width,
                                               table.style.left_indent, solve);
            if (!settled) {
                CHECK(column_widths(policy, widest.data(), narrowest.data(), columns, width,
                                    table.style.left_indent, solve));
            }
            // The longest words are asked for exactly when the table does not
            // fit under the default policy.
            const bool floors_read = solve.regime == SolveRegime::SQUEEZED ||
                                     solve.regime == SolveRegime::FLOORS ||
                                     solve.regime == SolveRegime::RIGID;
            CHECK_EQ(settled, !floors_read);
            CHECK(solve.refused == PolicyRefusal::NONE);
            CHECK(!solve.overflowed);
            CHECK_EQ(whole.narrowest.empty(), !floors_read);

            const size_t first = reader.position();
            REQUIRE(reader.next_is("solve"));
            const size_t end =
                blocks_golden::group_end(file, first, reader.end(), "solve", "endsolve");
            const std::vector<std::string> expected = reader.lines(first, end);
            CHECK_LINES_EQ(expected,
                           tables_golden::written(solve, widest.data(), narrowest.data(), columns),
                           "the solve");
            CHECK_LINES_EQ(expected,
                           tables_golden::written(
                               whole.solve, whole.widest.data(),
                               whole.narrowest.empty() ? nullptr : whole.narrowest.data(), columns),
                           "the solve, from fin_align");
            while (reader.position() < end)
                reader.take();
            ++tally.regimes[static_cast<unsigned>(solve.regime) % 7];
        } else {
            CHECK(!reader.next_is("solve"));
            ++tally.empty_tables;
        }

        // The rows, the rule, and the list they go into.
        std::vector<TableBox> boxes;
        std::vector<Node> vlist;
        Scaled prev_depth = IGNORE_DEPTH;
        const auto append = [&](const Node& box, int64_t number, bool wrapped,
                                const Scaled* carried) {
            TableBox entry;
            entry.box = box;
            entry.row = number;
            entry.wrapped = wrapped;
            entry.prev_depth_in = prev_depth;
            const size_t before = vlist.size();
            entry.prev_depth_out =
                fin_row(context.arena, vlist, box, prev_depth, table.style.interline);
            entry.has_glue = vlist.size() - before == 2;
            entry.vlist_index = static_cast<uint32_t>(vlist.size() - 1);
            entry.prev_depth_carried = carried != nullptr ? *carried : entry.prev_depth_out;
            prev_depth = entry.prev_depth_carried;
            boxes.push_back(entry);
        };

        // What each row said of its reading-order indices, to hold against
        // the commands the table ships at the end.
        struct RowMap {
            std::vector<uint32_t> shipped;
            std::vector<uint32_t> cell_index_end;
            uint32_t item_count = 0;
            bool mapped = true;
        };
        std::vector<RowMap> maps;

        for (size_t r = 0; r < rows; ++r) {
            const golden::Record& row_open = reader.take("row");
            CHECK_CONTEXT("row %zu (line %d)", r, row_open.line);
            REQUIRE_EQ(row_open.u32(0), static_cast<uint32_t>(r));
            const bool expect_wrapped = row_open.raw(1) == "wrapped";
            CHECK(expect_wrapped || row_open.raw(1) == "fits");

            std::vector<NodeView> cells;
            for (const tables_golden::CellIn& cell : table.rows[r])
                cells.push_back(NodeView(cell.material));
            RowInput in;
            in.cells = cells.data();
            in.cell_count = cells.size();
            in.natural = natural[r].data();
            in.columns = solve.columns.data();
            in.column_count = solve.columns.size();
            in.aligns = table.aligns.data();
            in.align_count = table.aligns.size();
            SetRow set;
            CHECK(aligner.set_row(context.arena, in, table.style, params, measure, set, &trace));
            REQUIRE_EQ(set.wrapped, expect_wrapped);
            const Box strutted = context.arena.box(set.box);
            maps.push_back(RowMap{set.shipped, set.cell_index_end, set.item_count, set.mapped});

            if (!set.wrapped) {
                CHECK_LINES_EQ(take_node_lines(reader, context),
                               tables_golden::box_lines(context, set.unstrutted),
                               "the row before its strut");
                CHECK(trace.cells.empty());
                CHECK_EQ(set.lowest, Scaled(0));
                CHECK_EQ(set.prev_depth, strutted.depth);
                ++tally.rows_fit;
            } else {
                REQUIRE_EQ(trace.cells.size(), columns);
                Scaled lowest = 0;
                for (size_t col = 0; col < columns; ++col) {
                    CHECK_CONTEXT("cell %zu", col);
                    const CellTrace& cell = trace.cells[col];
                    golden::Cursor record(reader.take("cell"));
                    CHECK_EQ(record.u32(), static_cast<uint32_t>(col));
                    CHECK_EQ(record.i64(), cell.column_width);
                    CHECK_EQ(record.i64(), cell.inner);
                    record.expect_done();
                    CHECK_EQ(cell.column_width, solve.columns[col]);

                    // What was broken is the cell's `unshrinkable`; a column
                    // the row has no cell for is broken from nothing.
                    std::vector<std::string> broken;
                    core_golden::write_nodes(context, "unshrinkable", cell.broken, broken);
                    if (col < table.rows[r].size())
                        CHECK_LINES_EQ(table.rows[r][col].unshrinkable, broken,
                                       "the list that was broken");
                    else
                        CHECK(cell.broken.empty());
                    check_break(reader, context, "unshrinkable", cell.inner, cell.params,
                                cell.paragraph);

                    const uint32_t line_count = reader.take("lines").u32(0);
                    CHECK_EQ(cell.lines.size(), static_cast<size_t>(line_count));
                    CHECK_EQ(cell.paragraph.lines.size(), static_cast<size_t>(line_count));
                    for (uint32_t l = 0; l < line_count && !reader.done(); ++l) {
                        CHECK_CONTEXT("line %u", l);
                        golden::Cursor appended(reader.take("append"));
                        const Scaled prev_in = appended.i64();
                        const Scaled prev_out = appended.i64();
                        appended.expect_done();
                        std::vector<std::string> glue_lines;
                        if (reader.next_is("glue"))
                            glue_lines = take_node_lines(reader, context);
                        const size_t at = reader.position();
                        reader.take("dims");
                        if (l >= cell.lines.size())
                            continue;
                        const CellTrace::Line& line = cell.lines[l];
                        CHECK_EQ(line.prev_depth_in, prev_in);
                        CHECK_EQ(line.prev_depth_out, prev_out);
                        CHECK_EQ(line.has_glue, !glue_lines.empty());
                        if (line.has_glue)
                            CHECK_LINES_EQ(glue_lines, core_golden::node_lines(context, line.glue),
                                           "the interline glue");
                        CHECK_EQ(reader.line_text(at), tables_golden::dims("dims", line.box, true));
                        // The padding and the strut change three numbers of
                        // the breaker's line and nothing else.
                        if (l < cell.paragraph.lines.size()) {
                            const Box& before = context.arena.box(cell.paragraph.lines[l].box);
                            CHECK(line.box.contents == before.contents);
                            CHECK(line.box.glue_set == before.glue_set &&
                                  line.box.glue_sign == before.glue_sign &&
                                  line.box.glue_order == before.glue_order &&
                                  line.box.width == before.width);
                        }
                        ++tally.cell_lines;
                    }
                    {
                        const size_t at = reader.position();
                        reader.take("stacked");
                        CHECK_EQ(reader.line_text(at),
                                 tables_golden::dims("stacked", cell.stacked, false));
                    }
                    {
                        const size_t at = reader.position();
                        reader.take("vtop");
                        CHECK_EQ(reader.line_text(at),
                                 tables_golden::dims("vtop", cell.vtop, false) +
                                     check::format(" %lld", static_cast<long long>(cell.below)));
                        CHECK(cell.vtop.kind == BoxKind::VLIST);
                    }
                    reader.take("endcell");
                    if (cell.below > lowest)
                        lowest = cell.below;
                    ++tally.wrapped_cells;
                }
                {
                    const size_t at = reader.position();
                    reader.take("rowbox");
                    CHECK_EQ(reader.line_text(at),
                             "rowbox " + blocks_golden::box_header(set.unstrutted));
                }
                CHECK_EQ(reader.take("lowest").i64(0), set.lowest);
                CHECK_EQ(set.lowest, lowest);
                ++tally.rows_wrapped;
            }
            {
                const size_t at = reader.position();
                reader.take("strutted");
                CHECK_EQ(reader.line_text(at), tables_golden::dims("strutted", strutted, true));
                // The strut changes the row's height and depth and nothing
                // else.
                CHECK(strutted.contents == set.unstrutted.contents);
                CHECK(strutted.width == set.unstrutted.width &&
                      strutted.glue_set == set.unstrutted.glue_set);
            }
            if (set.wrapped) {
                CHECK_EQ(reader.take("resumed").i64(0), set.prev_depth);
                tally.resumed_differs += set.prev_depth != strutted.depth ? 1 : 0;
            }
            reader.take("endrow");
            append(set.box, static_cast<int64_t>(r), set.wrapped, &set.prev_depth);

            const bool ruled = r == 0 && table.has_header && table.style.rule_thickness != 0;
            CHECK_EQ(reader.next_is("headrule"), ruled);
            if (reader.next_is("headrule")) {
                reader.take();
                PackError error = PackError::NONE;
                const Node rule_box = aligner.head_rule(context.arena, solve.columns.data(),
                                                        solve.columns.size(), table.style, &error);
                CHECK(error == PackError::NONE);
                CHECK_LINES_EQ(take_node_lines(reader, context),
                               core_golden::node_lines(context, rule_box), "the head rule");
                append(rule_box, -1, false, nullptr);
                ++tally.head_rules;
            }
        }

        // The stacking, by the steps and from fin_align.
        {
            const size_t first = reader.position();
            REQUIRE(reader.next_is("stack"));
            const size_t end =
                blocks_golden::group_end(file, first, reader.end(), "stack", "endstack");
            const std::vector<std::string> expected = reader.lines(first, end);
            CHECK_LINES_EQ(expected, tables_golden::written(context, boxes, vlist), "the stacking");
            CHECK_LINES_EQ(expected, tables_golden::written(context, whole.boxes, whole.vlist),
                           "the stacking, from fin_align");
            while (reader.position() < end)
                reader.take();
            tally.stacked += static_cast<int>(boxes.size());
        }
        // fin_align's boxes are the steps' boxes.
        REQUIRE_EQ(whole.boxes.size(), boxes.size());
        for (size_t i = 0; i < boxes.size(); ++i) {
            CHECK_CONTEXT("box %zu of the table", i);
            CHECK_EQ(whole.boxes[i].wrapped, boxes[i].wrapped);
            CHECK(deep_equal(context.arena, whole.boxes[i].box, context.arena, boxes[i].box));
        }
        CHECK(deep_equal(context.arena, NodeView(whole.vlist), context.arena, NodeView(vlist)));

        const PackResult packed = vpack(context.arena, vlist);
        CHECK(packed.error == PackError::NONE);
        const Node box = context.arena.add_box(packed.box, vlist);
        {
            golden::Cursor extents(reader.take("extents"));
            CHECK_EQ(extents.u32(), 1u);
            CHECK_EQ(extents.i64(), Scaled(0));
            CHECK_EQ(extents.i64(), whole.extent);
            extents.expect_done();
            CHECK_EQ(whole.extent, packed.box.height + packed.box.depth);
        }
        {
            const size_t at = reader.position();
            reader.take("vbox");
            CHECK(packed.box.kind == BoxKind::VLIST);
            CHECK_EQ(reader.line_text(at), tables_golden::dims("vbox", packed.box, false));
            CHECK_EQ(reader.line_text(at), tables_golden::dims("vbox", whole.packed.box, false));
            golden::Cursor origin(reader.take("origin"));
            CHECK_EQ(origin.i64(), Scaled(0));
            CHECK_EQ(origin.i64(), packed.box.height);
        }
        {
            const size_t first = reader.position();
            core_golden::read_cmds(reader, context);
            const std::vector<std::string> expected = reader.lines(first, reader.position());
            cmds.clear();
            CHECK(ship(context.arena, box, Point{0, packed.box.height}, measure, cmds) ==
                  ShipError::NONE);
            CHECK_LINES_EQ(expected, blocks_golden::written(context, cmds), "the shipped commands");
            tally.cmds += cmds.size();

            // The index map of every row, against these commands. The rows'
            // counts add up to the indices the lines hold; each cell owns at
            // least one index and the cells tile the row; every character of
            // a cell's material is the glyph with its index, every glue a set
            // glue, every box an atom; and each item not mapped is one of the
            // glues the table added (an \hss, a skip of a wrapped line).
            uint32_t items = 0;
            for (const DrawCmd& cmd : cmds) {
                if (cmd.is(CmdType::LINE))
                    items = std::max(items, cmd.end);
            }
            std::vector<const DrawCmd*> glyph_at(items, nullptr);
            std::vector<const DrawCmd*> glue_at(items, nullptr);
            for (const DrawCmd& cmd : cmds) {
                if (cmd.is(CmdType::GLYPH) && cmd.index < items && glyph_at[cmd.index] == nullptr)
                    glyph_at[cmd.index] = &cmd;
                if (cmd.is_set_glue() && cmd.index < items)
                    glue_at[cmd.index] = &cmd;
            }
            std::vector<uint8_t> claimed(items, 0);
            uint32_t base = 0;
            REQUIRE_EQ(maps.size(), rows);
            for (size_t r = 0; r < rows; ++r) {
                CHECK_CONTEXT("the index map of row %zu", r);
                const RowMap& map = maps[r];
                CHECK(map.mapped);
                REQUIRE_EQ(map.cell_index_end.size(), columns);
                uint32_t cell_first = 0;
                for (size_t col = 0; col < columns; ++col) {
                    CHECK(map.cell_index_end[col] > cell_first);
                    cell_first = map.cell_index_end[col];
                }
                CHECK_EQ(cell_first, map.item_count);
                size_t at = 0;
                for (size_t col = 0; col < table.rows[r].size() && col < columns; ++col) {
                    const std::vector<Node>& material = table.rows[r][col].material;
                    const uint32_t low = col == 0 ? 0 : map.cell_index_end[col - 1];
                    const uint32_t high = map.cell_index_end[col];
                    for (const Node& node : material) {
                        REQUIRE(at < map.shipped.size());
                        const uint32_t local = map.shipped[at++];
                        if (local == NOT_SHIPPED) {
                            CHECK(!node.is(NodeType::CHAR) && !node.is(NodeType::BOX));
                            continue;
                        }
                        CHECK(local >= low && local < high);
                        const uint32_t index = base + local;
                        REQUIRE(index < items);
                        CHECK_EQ(claimed[index], uint8_t(0));
                        claimed[index] = 1;
                        ++tally.mapped_nodes;
                        if (node.is(NodeType::CHAR)) {
                            REQUIRE(glyph_at[index] != nullptr);
                            CHECK_EQ(glyph_at[index]->codepoint, node.codepoint());
                            CHECK(glyph_at[index]->font() == node.font());
                        } else {
                            REQUIRE(glue_at[index] != nullptr);
                            CHECK_EQ(glue_at[index]->is(CmdType::ATOM), node.is(NodeType::BOX));
                        }
                    }
                }
                CHECK_EQ(at, map.shipped.size());
                base += map.item_count;
            }
            CHECK_EQ(base, items);
            for (uint32_t index = 0; index < items; ++index) {
                if (claimed[index] != 0)
                    continue;
                ++tally.added_items;
                // Not a cell's own: a glue, and no character's.
                CHECK(glue_at[index] != nullptr && glue_at[index]->is(CmdType::SET_GLUE));
                CHECK(glyph_at[index] == nullptr);
            }
            cmds.clear();
            CHECK(ship(context.arena, whole.box, Point{0, whole.packed.box.height}, measure,
                       cmds) == ShipError::NONE);
            CHECK_LINES_EQ(expected, blocks_golden::written(context, cmds),
                           "the shipped commands, from fin_align");
        }
        reader.take("endlayout");
        ++tally.layouts;
    }
    CHECK(reader.done());
    CHECK_EQ(table_measure.misses, 0);
    ++tally.cases;
}

const char* const TABLE_FILES[] = {"tables/widths.golden", "tables/shapes.golden",
                                   "tables/demo.golden"};

} // namespace

TEST(the_table_goldens_replay_record_by_record)
{
    // A golden directory that went missing must not read as a pass.
    const std::vector<std::string> files = golden::list("tables");
    REQUIRE(!files.empty());

    Tally total;
    for (const char* name : TABLE_FILES) {
        CHECK_CONTEXT("%s", name);
        golden::File file;
        if (!file.load(golden::path(name))) {
            FAIL("cannot load %s", name);
            continue;
        }
        const std::vector<core_golden::Case> cases = core_golden::cases(file);
        CHECK(!cases.empty());
        Tally tally;
        for (const core_golden::Case& c : cases) {
            CHECK_CONTEXT("case %s (line %d)", c.name.c_str(), c.line);
            if (c.kind != "table") {
                ++tally.skipped;
                std::printf("    skipped: %s case %s of %s\n", c.kind.c_str(), c.name.c_str(),
                            name);
                continue;
            }
            replay_case(file, c, tally);
        }
        std::printf("    %s: %d cases (%d skipped), %d layouts (%d of a table with no rows), "
                    "rows %d fit / %d wrapped, %d head rules, %d boxes stacked\n",
                    name, tally.cases, tally.skipped, tally.layouts, tally.empty_tables,
                    tally.rows_fit, tally.rows_wrapped, tally.head_rules, tally.stacked);
        std::printf("        solves: given %d, fractions %d, apportioned %d, unscaled %d, "
                    "squeezed %d, floors %d, rigid %d\n",
                    tally.regimes[0], tally.regimes[1], tally.regimes[2], tally.regimes[3],
                    tally.regimes[4], tally.regimes[5], tally.regimes[6]);
        std::printf("        %d cells (%d inline formulas), %d longest words over %d lines, "
                    "%d wrapped cells of %d lines, %d wrapped rows resuming from another "
                    "depth than their own, %zu commands\n",
                    tally.cells, tally.atoms, tally.words, tally.word_lines, tally.wrapped_cells,
                    tally.cell_lines, tally.resumed_differs, tally.cmds);
        total.cases += tally.cases;
        total.skipped += tally.skipped;
        total.layouts += tally.layouts;
        total.empty_tables += tally.empty_tables;
        total.rows_fit += tally.rows_fit;
        total.rows_wrapped += tally.rows_wrapped;
        total.head_rules += tally.head_rules;
        total.resumed_differs += tally.resumed_differs;
        total.atoms += tally.atoms;
        for (int i = 0; i < 7; ++i)
            total.regimes[i] += tally.regimes[i];
    }
    std::printf("    in all: %d cases (%d skipped), %d layouts, rows %d fit / %d wrapped; solves: "
                "given %d, fractions %d, apportioned %d, unscaled %d, squeezed %d, floors %d, "
                "rigid %d\n",
                total.cases, total.skipped, total.layouts, total.rows_fit, total.rows_wrapped,
                total.regimes[0], total.regimes[1], total.regimes[2], total.regimes[3],
                total.regimes[4], total.regimes[5], total.regimes[6]);

    // What the three files hold, counted from their records: a case that was
    // skipped or a file that shrank must not read as a pass.
    CHECK_EQ(total.cases, 36);
    CHECK_EQ(total.skipped, 0);
    CHECK_EQ(total.layouts, 97);
    CHECK_EQ(total.empty_tables, 1);
    CHECK_EQ(total.rows_fit, 237);
    CHECK_EQ(total.rows_wrapped, 204);
    CHECK_EQ(total.regimes[static_cast<int>(SolveRegime::GIVEN)], 5);
    CHECK_EQ(total.regimes[static_cast<int>(SolveRegime::FRACTIONS)], 9);
    CHECK_EQ(total.regimes[static_cast<int>(SolveRegime::APPORTIONED)], 26);
    CHECK_EQ(total.regimes[static_cast<int>(SolveRegime::UNSCALED)], 1);
    CHECK_EQ(total.regimes[static_cast<int>(SolveRegime::SQUEEZED)], 29);
    CHECK_EQ(total.regimes[static_cast<int>(SolveRegime::FLOORS)], 26);
    // No golden reaches it (FORMAT-tables.md section 3.3); see
    // a_table_with_nothing_to_give_is_rigid.
    CHECK_EQ(total.regimes[static_cast<int>(SolveRegime::RIGID)], 0);
    CHECK(total.head_rules > 0);
    CHECK(total.resumed_differs > 0);
    CHECK(total.atoms > 0);
}

// --- Without a golden ---------------------------------------------------------------------

namespace
{

int g_violations = 0;

void count_violation(const char*)
{
    ++g_violations;
}

// Counts the contracts broken while it is alive: none may be, by data.
class NoContractBroken
{
public:
    NoContractBroken()
        : _before(contract_handler())
    {
        g_violations = 0;
        contract_handler() = &count_violation;
    }
    ~NoContractBroken() { contract_handler() = _before; }
    int broken() const { return g_violations; }

private:
    ContractHandler _before;
};

// Every character five points wide, seven high and two deep.
CharDimens fixed_dimens(void*, FontKey, uint32_t)
{
    return CharDimens{5 * UNITY, 7 * UNITY, 2 * UNITY};
}

CharMeasure fixed_measure()
{
    return CharMeasure{&fixed_dimens, nullptr};
}

const GlueSpec WORD_SPACE = {3 * UNITY, UNITY, GlueOrder::NORMAL, UNITY, GlueOrder::NORMAL};

// `words` words of `letters` letters, a space between them.
std::vector<Node> text(size_t words, size_t letters)
{
    const FontKey font = FontKey::make(0, 10 * UNITY);
    std::vector<Node> out;
    for (size_t w = 0; w < words; ++w) {
        if (w != 0)
            out.push_back(glue(WORD_SPACE));
        for (size_t l = 0; l < letters; ++l)
            out.push_back(char_node(font, 'a'));
    }
    return out;
}

TableStyle plain_style()
{
    TableStyle style;
    style.cell_padding = 2 * UNITY;
    style.rule_thickness = UNITY / 2;
    style.interline.baselineskip = GlueSpec{12 * UNITY, 0, GlueOrder::NORMAL, 0, GlueOrder::NORMAL};
    style.interline.lineskip = GlueSpec{UNITY, 0, GlueOrder::NORMAL, 0, GlueOrder::NORMAL};
    style.strut = Strut::filling(12 * UNITY);
    return style;
}

// A table whose every row has the cells it is given, by reference.
struct Table {
    std::vector<NodeView> cells;
    std::vector<uint32_t> row_first{0};
    TableInput in;

    void row(const std::vector<const std::vector<Node>*>& of)
    {
        for (const std::vector<Node>* cell : of)
            cells.push_back(NodeView(*cell));
        row_first.push_back(static_cast<uint32_t>(cells.size()));
    }

    const TableInput& input(const TableStyle& style)
    {
        in.cells = cells.data();
        in.row_first = row_first.data();
        in.rows = row_first.size() - 1;
        in.style = style;
        return in;
    }
};

Scaled sum_of(const std::vector<Scaled>& values)
{
    Scaled total = 0;
    for (const Scaled value : values)
        total += value;
    return total;
}

} // namespace

TEST(a_table_with_no_rows_or_no_columns_is_an_empty_box)
{
    NoContractBroken contracts;
    NodeArena arena;
    Aligner aligner;
    Aligned out;

    // No rows, by every way of saying so.
    TableInput none;
    none.style = plain_style();
    none.has_header = true;
    CHECK(aligner.fin_align(arena, none, 100 * UNITY, prose_breaking(), fixed_measure(), out));
    CHECK(out.boxes.empty() && out.vlist.empty() && out.solve.columns.empty());
    CHECK_EQ(out.packed.box.width, Scaled(0));
    CHECK_EQ(out.extent, Scaled(0));
    CHECK(out.box.is(NodeType::BOX));
    none.rows = 3; // and no arrays
    CHECK(aligner.fin_align(arena, none, 100 * UNITY, prose_breaking(), fixed_measure(), out));
    CHECK(out.boxes.empty());

    // Rows with no cells: no columns. Each row is its indent and its strut.
    Table table;
    table.row({});
    table.row({});
    TableStyle style = plain_style();
    style.left_indent = 7 * UNITY;
    table.in.has_header = true;
    CHECK(aligner.fin_align(arena, table.input(style), 100 * UNITY, prose_breaking(),
                            fixed_measure(), out));
    CHECK(out.solve.regime == SolveRegime::UNSCALED);
    CHECK(out.solve.columns.empty());
    REQUIRE_EQ(out.boxes.size(), size_t(3)); // two rows and the rule
    CHECK_EQ(arena.box(out.boxes[0].box).width, 7 * UNITY);
    CHECK_EQ(arena.box(out.boxes[0].box).height, style.strut.height);
    CHECK(!out.boxes[0].wrapped);
    CHECK_EQ(out.boxes[1].row, int64_t(-1));
    CHECK_EQ(arena.box(out.boxes[1].box).width, 7 * UNITY);
    CHECK_EQ(contracts.broken(), 0);
}

TEST(ragged_rows_and_empty_cells_are_set_as_empty_columns)
{
    NoContractBroken contracts;
    NodeArena arena;
    Aligner aligner;
    Aligned out;
    const std::vector<Node> word = text(1, 4);
    const std::vector<Node> prose = text(12, 4);
    const std::vector<Node> empty;

    Table table;
    table.row({&word});
    table.row({&empty, &prose, &word});
    table.row({});
    table.row({&word, &empty});
    const TableStyle style = plain_style();

    // Wide enough for everything: no row wraps, and every row has a box for
    // every column.
    CHECK(aligner.fin_align(arena, table.input(style), 1000 * UNITY, prose_breaking(),
                            fixed_measure(), out));
    CHECK(out.solve.regime == SolveRegime::APPORTIONED);
    REQUIRE_EQ(out.solve.columns.size(), size_t(3));
    CHECK_EQ(sum_of(out.solve.columns), 1000 * UNITY);
    REQUIRE_EQ(out.boxes.size(), size_t(4));
    for (const TableBox& box : out.boxes) {
        CHECK(!box.wrapped);
        CHECK_EQ(arena.contents(box.box).size(), size_t(4));
        CHECK_EQ(arena.box(box.box).width, 1000 * UNITY);
    }

    // Narrow: the row with the prose wraps, and the rows without it do not,
    // whatever columns they lack.
    CHECK(aligner.fin_align(arena, table.input(style), 150 * UNITY, prose_breaking(),
                            fixed_measure(), out));
    CHECK(out.solve.regime == SolveRegime::SQUEEZED);
    CHECK_EQ(sum_of(out.solve.columns), 150 * UNITY);
    REQUIRE_EQ(out.boxes.size(), size_t(4));
    CHECK(!out.boxes[0].wrapped);
    CHECK(out.boxes[1].wrapped);
    CHECK(!out.boxes[2].wrapped);
    CHECK(!out.boxes[3].wrapped);
    // The row after the wrapped one is spaced from its last line: the depth
    // carried is a strut's, not the row's.
    CHECK(arena.box(out.boxes[1].box).depth > style.strut.depth);
    CHECK_EQ(out.boxes[1].prev_depth_carried, style.strut.depth);
    CHECK_EQ(out.boxes[2].prev_depth_in, style.strut.depth);
    CHECK_EQ(out.boxes[1].prev_depth_out, arena.box(out.boxes[1].box).depth);
    CHECK_EQ(contracts.broken(), 0);
}

TEST(a_table_with_nothing_to_give_is_rigid)
{
    // The regime no golden reaches: the widest cells do not fit, the floors
    // do, and no column is wider than its floor. column_widths cannot be led
    // there by cells (a floor is never wider than its cell), only by numbers.
    const Scaled widest[] = {40, 60};
    const Scaled narrowest[] = {40, 60};
    ColumnSolve solve;
    solve.available = 90;
    squeezed(widest, narrowest, 2, solve);
    CHECK(solve.regime == SolveRegime::FLOORS);
    CHECK_EQ(solve.slack, Scaled(-10));

    solve.available = 100;
    squeezed(widest, narrowest, 2, solve);
    CHECK(solve.regime == SolveRegime::FLOORS);
    CHECK_EQ(solve.slack, Scaled(0));

    solve.available = 130;
    squeezed(widest, narrowest, 2, solve);
    CHECK(solve.regime == SolveRegime::RIGID);
    CHECK_EQ(solve.slack, Scaled(30));
    CHECK_EQ(solve.total, Scaled(0));
    REQUIRE_EQ(solve.columns.size(), size_t(2));
    CHECK_EQ(solve.columns[0], Scaled(40));
    CHECK_EQ(solve.columns[1], Scaled(60));
    REQUIRE_EQ(solve.give.size(), size_t(2));
    CHECK_EQ(solve.give[0], Scaled(0));

    // Floors wider than the widest cells give a negative total: rigid too.
    const Scaled wider[] = {50, 70};
    solve.available = 130;
    squeezed(widest, wider, 2, solve);
    CHECK(solve.regime == SolveRegime::RIGID);
    CHECK_EQ(solve.total, Scaled(-20));
}

TEST(the_longest_words_are_asked_for_only_when_the_table_does_not_fit)
{
    const Scaled widest[] = {100 * UNITY, 200 * UNITY};
    const Scaled narrowest[] = {30 * UNITY, 40 * UNITY};
    ColumnSolve solve;
    CHECK(column_widths(ColumnPolicy(), widest, nullptr, 2, 300 * UNITY, 0, solve));
    CHECK(solve.regime == SolveRegime::APPORTIONED);
    CHECK_EQ(solve.columns[0], 100 * UNITY);

    CHECK(!column_widths(ColumnPolicy(), widest, nullptr, 2, 300 * UNITY - 1, 0, solve));
    CHECK(solve.columns.empty());
    CHECK_EQ(solve.available, 300 * UNITY - 1);
    CHECK(column_widths(ColumnPolicy(), widest, narrowest, 2, 300 * UNITY - 1, 0, solve));
    CHECK(solve.regime == SolveRegime::SQUEEZED);
    CHECK_EQ(sum_of(solve.columns), 300 * UNITY - 1);

    // The indent comes off the measure, and the measure is never under a
    // point.
    CHECK(!column_widths(ColumnPolicy(), widest, nullptr, 2, 300 * UNITY, 1, solve));
    CHECK(column_widths(ColumnPolicy(), widest, narrowest, 2, -5, 0, solve));
    CHECK_EQ(solve.available, UNITY);
    CHECK(solve.regime == SolveRegime::FLOORS);

    // A caller's widths need neither.
    const Scaled given[] = {7, 9};
    ColumnPolicy policy;
    policy.kind = WidthPolicy::WIDTHS;
    policy.widths = given;
    policy.count = 2;
    CHECK(column_widths(policy, nullptr, nullptr, 2, 300 * UNITY, 0, solve));
    CHECK(solve.regime == SolveRegime::GIVEN);
    CHECK_EQ(solve.columns[1], Scaled(9));
}

TEST(a_policy_the_reference_would_refuse_falls_back_and_says_so)
{
    NoContractBroken contracts;
    const Scaled widest[] = {100, 200, 300};
    const Scaled narrowest[] = {10, 20, 30};
    const Scaled width = 10 * UNITY;
    ColumnSolve solve;
    const auto refused = [&](const ColumnPolicy& policy, PolicyRefusal why) {
        CHECK(column_widths(policy, widest, narrowest, 3, width, 0, solve));
        CHECK(solve.refused == why);
        CHECK(solve.regime == SolveRegime::APPORTIONED);
        REQUIRE_EQ(solve.columns.size(), size_t(3));
        CHECK_EQ(sum_of(solve.columns), width);
    };

    ColumnPolicy policy;
    policy.kind = WidthPolicy::WIDTHS;
    const Scaled two[] = {5, 6};
    policy.widths = two;
    policy.count = 2;
    refused(policy, PolicyRefusal::WRONG_COUNT);
    const Scaled zero[] = {5, 0, 6};
    policy.widths = zero;
    policy.count = 3;
    refused(policy, PolicyRefusal::WIDTH_BELOW_ONE);
    const Scaled negative[] = {5, 6, -1};
    policy.widths = negative;
    refused(policy, PolicyRefusal::WIDTH_BELOW_ONE);
    policy.widths = nullptr;
    refused(policy, PolicyRefusal::NO_ARRAY);
    // No entries at all is a table nobody sized.
    policy.count = 0;
    refused(policy, PolicyRefusal::NONE);
    // The least a width may be.
    const Scaled ones[] = {1, 1, 1};
    policy.widths = ones;
    policy.count = 3;
    CHECK(column_widths(policy, widest, narrowest, 3, width, 0, solve));
    CHECK(solve.regime == SolveRegime::GIVEN && solve.refused == PolicyRefusal::NONE);

    policy = ColumnPolicy();
    policy.kind = WidthPolicy::FRACTIONS;
    policy.count = 3;
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double bad[][3] = {
        {1.0, nan, 1.0},        {1.0, inf, 1.0},  {1.0, -inf, 1.0},      {1.0, 0.0, 1.0},
        {1.0, -0.0, 1.0},       {1.0, -2.0, 4.0}, {1e308, 1e308, 1e308}, // sums to infinity
        {1e300, 1e-320, 1e300}, // positive, and nothing as a share
    };
    for (const double* fractions : bad) {
        CHECK_CONTEXT("%g %g %g", fractions[0], fractions[1], fractions[2]);
        policy.fractions = fractions;
        refused(policy, PolicyRefusal::NOT_A_SHARE);
    }
    policy.fractions = nullptr;
    refused(policy, PolicyRefusal::NO_ARRAY);
    const double good[] = {1.0, 2.0, 1.0};
    policy.fractions = good;
    policy.count = 2;
    refused(policy, PolicyRefusal::WRONG_COUNT);
    policy.count = 3;
    CHECK(column_widths(policy, widest, narrowest, 3, width, 0, solve));
    CHECK(solve.regime == SolveRegime::FRACTIONS && solve.refused == PolicyRefusal::NONE);
    CHECK_EQ(solve.columns[0], width / 4);
    CHECK_EQ(solve.columns[1], width / 2);
    CHECK_EQ(sum_of(solve.columns), width);

    // A kind that is no kind is the default.
    policy.kind = static_cast<WidthPolicy>(200);
    refused(policy, PolicyRefusal::NONE);
    CHECK_EQ(contracts.broken(), 0);
}

TEST(shares_round_half_to_even_and_the_last_column_borrows_from_the_widest)
{
    ColumnSolve solve;
    // 65537 in four equal shares is 16384.25 each; in two, 32768.5, which
    // rounds to the even 32768 twice, and the last column takes the 1 left.
    solve.available = 65537;
    const double halves[] = {1.0, 1.0};
    REQUIRE(shared(halves, 2, solve));
    CHECK_EQ(solve.floored[0], Scaled(32768));
    CHECK_EQ(solve.floored[1], Scaled(32768));
    CHECK_EQ(solve.columns[1], Scaled(32769));
    CHECK_EQ(solve.borrowed, int64_t(-1));
    // 65539 in two is 32769.5: the even neighbour is 32770, twice, and the
    // last column gives one back.
    solve.available = 65539;
    REQUIRE(shared(halves, 2, solve));
    CHECK_EQ(solve.floored[0], Scaled(32770));
    CHECK_EQ(solve.columns[1], Scaled(32769));

    // Shares that round to nothing are a scaled point each, and what they
    // cost comes out of the first of the widest columns, not the last.
    solve.available = UNITY;
    const double tiny[] = {1.0, 1.0, 1e-9, 1e-9};
    REQUIRE(shared(tiny, 4, solve));
    CHECK_EQ(solve.floored[0], Scaled(32768));
    CHECK_EQ(solve.floored[2], Scaled(1));
    CHECK_EQ(solve.floored[3], Scaled(1));
    // 32768 + 32768 + 1 + 1 is two over, so the last column is left at -1 and
    // needs two to be 1.
    CHECK_EQ(solve.last, Scaled(-1));
    CHECK_EQ(solve.borrowed, int64_t(0));
    CHECK_EQ(solve.columns[0], Scaled(32766));
    CHECK_EQ(solve.columns[1], Scaled(32768));
    CHECK_EQ(solve.columns[3], Scaled(1));
    CHECK_EQ(sum_of(solve.columns), UNITY);
}

TEST(sums_and_products_past_a_scaled_are_defined_and_reported)
{
    NoContractBroken contracts;
    ColumnSolve solve;

    // A product that needs more than 64 bits, exact where the compiler has
    // them: (3*2^40 + 1)(2^30 + 1) // 2^42 = 3*2^28, and (2^40 - 1)(2^30 + 1)
    // // 2^42 = 2^28.
    const Scaled big[] = {3 * (Scaled(1) << 40) + 1, (Scaled(1) << 40) - 1};
    solve.available = (Scaled(1) << 30) + 1;
    apportioned(big, 2, solve);
    CHECK(solve.regime == SolveRegime::APPORTIONED);
    CHECK_EQ(solve.total, Scaled(1) << 42);
#if defined(__SIZEOF_INT128__)
    CHECK(!solve.overflowed);
    CHECK_EQ(solve.floored[0], 3 * (Scaled(1) << 28));
    CHECK_EQ(solve.floored[1], Scaled(1) << 28);
    CHECK_EQ(solve.columns[1], (Scaled(1) << 28) + 1);
#else
    CHECK(solve.overflowed);
#endif

    // Widths whose sum leaves a Scaled.
    const Scaled huge[] = {SCALED_MAX, SCALED_MAX, 5};
    const Scaled floors[] = {SCALED_MAX, 1, SCALED_MAX};
    CHECK(column_widths(ColumnPolicy(), huge, floors, 3, 100 * UNITY, 0, solve));
    CHECK(solve.overflowed);
    CHECK_EQ(solve.columns.size(), size_t(3));
    CHECK(column_widths(ColumnPolicy(), huge, nullptr, 3, SCALED_MAX, -SCALED_MAX, solve) == false);
    const Scaled negative[] = {-SCALED_MAX, -SCALED_MAX, -5};
    CHECK(column_widths(ColumnPolicy(), negative, floors, 3, -SCALED_MAX, SCALED_MAX, solve));
    CHECK_EQ(solve.available, UNITY);
    CHECK_EQ(solve.columns.size(), size_t(3));

    // Rows set to such widths, with a padding and an indent to match.
    NodeArena arena;
    Aligner aligner;
    const std::vector<Node> word = text(3, 5);
    const NodeView cells[] = {NodeView(word), NodeView(word), NodeView(word)};
    TableStyle style = plain_style();
    style.cell_padding = SCALED_MAX;
    style.left_indent = -SCALED_MAX;
    style.rule_thickness = SCALED_MAX;
    for (const Scaled* columns : {huge, floors, negative}) {
        RowInput in;
        in.cells = cells;
        in.cell_count = 3;
        in.columns = columns;
        in.column_count = 3;
        SetRow set;
        aligner.set_row(arena, in, style, prose_breaking(), fixed_measure(), set);
        CHECK(set.box.is(NodeType::BOX));
        CHECK(arena.find_box(set.box) != nullptr);
        PackError error = PackError::NONE;
        const Node ruled = aligner.head_rule(arena, columns, 3, style, &error);
        CHECK(ruled.is(NodeType::BOX));
        if (columns != floors)
            CHECK(error != PackError::NONE);
    }
    std::vector<Scaled> widths;
    CHECK(!fin_col(widths, MAX_ALIGN_COLUMNS, 1));
    CHECK(widths.empty());
    CHECK_EQ(contracts.broken(), 0);
}

TEST(a_hand_built_cell_is_set_without_a_contract_being_broken)
{
    NoContractBroken contracts;
    NodeArena arena;
    Aligner aligner;
    const CharMeasure measure = fixed_measure();

    // A box nested as deep as an arena allows, a BOX and a DISC node that name
    // nothing, a rule of running width, and glue of every order.
    std::vector<Node> inner = text(1, 2);
    Node deep = arena.add_box(hpack(arena, inner, 0, PackMode::ADDITIONAL, measure).box, inner);
    for (int level = 1; level < NodeArena::MAX_NESTING; ++level) {
        const std::vector<Node> one{deep};
        deep = arena.add_box(hpack(arena, one, 0, PackMode::ADDITIONAL, measure).box, one);
    }
    REQUIRE_EQ(arena.nesting(deep), NodeArena::MAX_NESTING);
    Node nothing;
    nothing.type = NodeType::BOX;
    nothing.u = 0x7FFFFFFFu;
    Node no_disc;
    no_disc.type = NodeType::DISC;
    no_disc.u = 12345;

    std::vector<Node> hostile = text(2, 3);
    hostile.push_back(deep);
    hostile.push_back(glue(GlueSpec{UNITY, UNITY, GlueOrder::FILLL, UNITY, GlueOrder::FILLL}));
    hostile.push_back(nothing);
    hostile.push_back(penalty(EJECT_PENALTY));
    hostile.push_back(no_disc);
    hostile.push_back(kern(-MAX_DIMEN));
    hostile.push_back(math_on(UNITY));
    const std::vector<Node> prose = text(30, 4);
    // The same with a rule of running width, which no hlist may hold: packing
    // reports it and ship-out stops at it.
    std::vector<Node> running = hostile;
    running.push_back(rule());

    Table table;
    table.row({&hostile, &prose});
    table.row({&prose, &hostile});
    table.in.has_header = true;
    const ColumnAlign aligns[] = {ColumnAlign::RIGHT, static_cast<ColumnAlign>(77)};
    table.in.aligns = aligns;
    table.in.align_count = 2;
    Aligned out;
    for (const Scaled width : {Scaled(0), 50 * UNITY, 200 * UNITY, 5000 * UNITY}) {
        CHECK_CONTEXT("width %lld", static_cast<long long>(width));
        aligner.fin_align(arena, table.input(plain_style()), width, prose_breaking(), measure, out);
        // The list is malformed and the table says so; it is a table all the
        // same, and it ships.
        CHECK(!out.ok());
        REQUIRE_EQ(out.boxes.size(), size_t(3));
        REQUIRE(out.box.is(NodeType::BOX));
        std::vector<DrawCmd> cmds;
        CHECK(ship(arena, out.box, Point{0, out.packed.box.height}, measure, cmds) ==
              ShipError::NONE);
        CHECK(!cmds.empty());

        Table ruled;
        ruled.row({&running, &prose, &running});
        ruled.row({&running});
        Aligned other;
        aligner.fin_align(arena, ruled.input(plain_style()), width, prose_breaking(), measure,
                          other);
        CHECK(other.pack_error != PackError::NONE);
        REQUIRE_EQ(other.boxes.size(), size_t(2));
        cmds.clear();
        CHECK(ship(arena, other.box, Point{0, other.packed.box.height}, measure, cmds) !=
              ShipError::NONE);
    }
    CHECK_EQ(contracts.broken(), 0);

    // Offsets that run backwards, and an align count with no array.
    const NodeView cells[] = {NodeView(prose), NodeView(prose), NodeView(prose)};
    const uint32_t backwards[] = {2, 0, 3, 1};
    TableInput in;
    in.cells = cells;
    in.row_first = backwards;
    in.rows = 3;
    in.align_count = 9;
    in.style = plain_style();
    CHECK(aligner.fin_align(arena, in, 100 * UNITY, prose_breaking(), measure, out));
    REQUIRE_EQ(out.boxes.size(), size_t(3));
    CHECK_EQ(out.solve.columns.size(), size_t(3));
    CHECK_EQ(contracts.broken(), 0);
}

TEST(a_thousand_columns_by_sixty_four_rows_is_linear_work)
{
    NoContractBroken contracts;
    constexpr size_t COLUMNS = 1024;
    constexpr size_t ROWS = 64;
    const std::vector<Node> word = text(1, 3);
    const std::vector<Node> pair = text(2, 3);
    Table table;
    for (size_t r = 0; r < ROWS; ++r) {
        for (size_t c = 0; c < COLUMNS; ++c)
            table.cells.push_back(NodeView((r + c) % 7 == 0 ? pair : word));
        table.row_first.push_back(static_cast<uint32_t>(table.cells.size()));
    }
    table.in.has_header = true;
    const TableStyle style = plain_style();
    NodeArena arena;
    Aligner aligner;
    Aligned out;

    // One word is 15 pt and its cell 19 pt; two are 33 pt and 37 pt. At
    // 30 pt a column every cell fits; at 25 pt the pairs wrap.
    const Scaled fits = static_cast<Scaled>(COLUMNS) * 40 * UNITY;
    CHECK(
        aligner.fin_align(arena, table.input(style), fits, prose_breaking(), fixed_measure(), out));
    CHECK(out.solve.regime == SolveRegime::APPORTIONED);
    CHECK(out.narrowest.empty());
    CHECK_EQ(out.solve.columns.size(), COLUMNS);
    CHECK_EQ(sum_of(out.solve.columns), fits);
    REQUIRE_EQ(out.boxes.size(), ROWS + 1);
    for (const TableBox& box : out.boxes)
        CHECK(!box.wrapped);
    CHECK_EQ(out.packed.box.width, fits);
    const size_t fit_nodes = arena.node_count();

    arena.clear();
    const Scaled tight = static_cast<Scaled>(COLUMNS) * 25 * UNITY;
    CHECK(aligner.fin_align(arena, table.input(style), tight, prose_breaking(), fixed_measure(),
                            out));
    CHECK(out.solve.regime == SolveRegime::SQUEEZED);
    CHECK_EQ(out.narrowest.size(), COLUMNS);
    CHECK_EQ(sum_of(out.solve.columns), tight);
    size_t wrapped = 0;
    for (const TableBox& box : out.boxes)
        wrapped += box.wrapped ? 1 : 0;
    CHECK_EQ(wrapped, ROWS);
    CHECK_EQ(out.packed.box.width, tight);
    std::printf("    %zu x %zu cells: %zu nodes when every row fits, %zu nodes and %zu boxes "
                "when every row wraps\n",
                COLUMNS, ROWS, fit_nodes, arena.node_count(), arena.box_count());

    // The same table again in the same objects is the same table.
    Aligned again;
    NodeArena second;
    CHECK(aligner.fin_align(second, table.input(style), tight, prose_breaking(), fixed_measure(),
                            again));
    CHECK(deep_equal(arena, out.box, second, again.box));
    CHECK_EQ(contracts.broken(), 0);
}

TEST(sixty_five_thousand_one_character_cells_are_set)
{
    NoContractBroken contracts;
    constexpr size_t COLUMNS = 16;
    constexpr size_t ROWS = 4096;
    const std::vector<Node> letter = text(1, 1);
    Table table;
    for (size_t r = 0; r < ROWS; ++r) {
        for (size_t c = 0; c < COLUMNS; ++c)
            table.cells.push_back(NodeView(letter));
        table.row_first.push_back(static_cast<uint32_t>(table.cells.size()));
    }
    const TableStyle style = plain_style();
    NodeArena arena;
    Aligner aligner;
    Aligned out;

    // A measure of nothing: every column at its floor, which is its cell, so
    // no row wraps although the table does not fit.
    CHECK(aligner.fin_align(arena, table.input(style), 0, prose_breaking(), fixed_measure(), out));
    CHECK(out.solve.regime == SolveRegime::FLOORS);
    CHECK_EQ(out.natural.size(), COLUMNS * ROWS);
    REQUIRE_EQ(out.boxes.size(), ROWS);
    CHECK_EQ(out.solve.columns[0], 9 * UNITY);
    for (const TableBox& box : out.boxes)
        CHECK(!box.wrapped);
    CHECK_EQ(out.extent, static_cast<Scaled>(ROWS) * 12 * UNITY);

    // Columns a scaled point narrower than their cells: every cell of every
    // row is a paragraph of one overfull line.
    arena.clear();
    std::vector<Scaled> narrow(COLUMNS, 9 * UNITY - 1);
    table.in.policy.kind = WidthPolicy::WIDTHS;
    table.in.policy.widths = narrow.data();
    table.in.policy.count = COLUMNS;
    CHECK(aligner.fin_align(arena, table.input(style), 0, prose_breaking(), fixed_measure(), out));
    CHECK(out.solve.regime == SolveRegime::GIVEN);
    REQUIRE_EQ(out.boxes.size(), ROWS);
    for (const TableBox& box : out.boxes)
        CHECK(box.wrapped);
    // One line a cell, so a wrapped row resumes from its own depth and the
    // table is as tall as it was.
    CHECK_EQ(out.extent, static_cast<Scaled>(ROWS) * 12 * UNITY);
    std::printf("    %zu cells wrapped: %zu nodes, %zu boxes\n", COLUMNS * ROWS, arena.node_count(),
                arena.box_count());
    CHECK_EQ(contracts.broken(), 0);
}

TEST(setting_a_row_again_adds_what_it_added_the_first_time)
{
    // A row set twice to the same widths is the same row, and with the arena
    // rewound in between the second setting lands where the first did: the
    // Aligner keeps nothing of a row.
    NodeArena arena;
    Aligner aligner;
    const std::vector<Node> word = text(1, 4);
    const std::vector<Node> prose = text(9, 4);
    const NodeView cells[] = {NodeView(word), NodeView(prose)};
    const ColumnAlign aligns[] = {ColumnAlign::CENTER, ColumnAlign::RIGHT};
    for (const Scaled second : {300 * UNITY, 60 * UNITY}) {
        const Scaled columns[] = {30 * UNITY, second};
        RowInput in;
        in.cells = cells;
        in.cell_count = 2;
        in.columns = columns;
        in.column_count = 2;
        in.aligns = aligns;
        in.align_count = 2;
        const TableStyle style = plain_style();
        const NodeArena::Mark mark = arena.mark();
        SetRow first;
        CHECK(aligner.set_row(arena, in, style, prose_breaking(), fixed_measure(), first));
        CHECK_EQ(first.wrapped, second < 100 * UNITY);
        const size_t nodes = arena.node_count();
        const size_t boxes = arena.box_count();
        NodeArena copy;
        const Node kept = copy.import(arena, first.box);
        arena.rewind(mark);
        SetRow again;
        CHECK(aligner.set_row(arena, in, style, prose_breaking(), fixed_measure(), again));
        CHECK_EQ(arena.node_count(), nodes);
        CHECK_EQ(arena.box_count(), boxes);
        CHECK(again.box == first.box);
        CHECK(deep_equal(copy, kept, arena, again.box));
        CHECK_EQ(again.prev_depth, first.prev_depth);
        CHECK_EQ(again.lowest, first.lowest);
        // Without the widths' natural sizes given, they are measured here and
        // the answer is the same.
        Scaled natural[2];
        natural[0] = natural_width(arena, cells[0], style.cell_padding, fixed_measure());
        natural[1] = natural_width(arena, cells[1], style.cell_padding, fixed_measure());
        in.natural = natural;
        SetRow given;
        CHECK(aligner.set_row(arena, in, style, prose_breaking(), fixed_measure(), given));
        CHECK_EQ(given.wrapped, first.wrapped);
        CHECK(deep_equal(arena, given.box, arena, again.box));
    }
}
