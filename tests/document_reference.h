// How the replays of the reference's goldens through the public Document
// (tests/test_api_document.cpp, tests/test_api_document_golden.cpp) hold a
// public description to the golden's numbers.
//
// The reference's lengths are integers of scaled points and the header's are
// float pixels. NOTHING IS SUBSTITUTED AND NOTHING IS NEAR: every value the
// block layer lays a block out with is what the public description came to,
// and hold_block() compares each with the golden's by ==. A golden whose
// numbers a float of pixels does not carry would fail here, and be a finding.
//
// Two things reach a replay by another road than the header, and are not
// lengths:
//   - the reference's RULES, where the product has its own: the context's
//     TexRules (line breaking) and ImTypesetDocument::reference_rules
//     (stacking, and the first line narrowed by its indent). No public call
//     selects either; a test sets them, as use_reference() does;
//   - a RUN's font is not read back here: it is in every glyph of the block's
//     commands, which each replay compares with the golden's, exactly.

#pragma once

#include "blocks_golden.h"
#include "check.h"

#include "imtypeset.h"

#include "imtypeset_document.h"

namespace document_reference
{

inline float px_float(ImTypeset::detail::Scaled length, double ppi)
{
    return static_cast<float>(ImTypeset::detail::px_from_sp(length, ppi));
}

// Puts a new document under the reference's rules and hands it the golden's
// theme through ImTypesetDocumentOptions. Call it before the blocks are set.
// Returns why the theme cannot be said through the header, or null.
inline const char* use_reference(ImTypesetDocument* document, const blocks_golden::ThemeIn& theme)
{
    if (theme.theme.quote_indent <= 0 || theme.theme.quote_rule_thickness <= 0)
        return "a quote indent or bar of no width, which the header reads as the default";
    document->reference_rules = true;
    ImTypesetDocumentOptions options;
    options.QuoteIndent = px_float(theme.theme.quote_indent, theme.px_per_inch);
    options.QuoteBarThickness = px_float(theme.theme.quote_rule_thickness, theme.px_per_inch);
    CHECK(ImTypeset::SetDocumentOptions(document, &options));
    return nullptr;
}

// Block `number` as the document resolved its description, against the
// golden's block: call it once the document has been laid out. `hosted` is a
// block a host rectangle stands in for, whose height is `in.thickness`.
// `lengths` counts the lengths compared.
inline void hold_block(const ImTypesetDocument& document, size_t number,
                       const blocks_golden::BlockIn& in, bool hosted,
                       const blocks_golden::ThemeIn& theme, int& lengths)
{
    using namespace ImTypeset::detail;
    CHECK_CONTEXT("block %zu (line %d)", number, in.line);
    if (!CHECK(number < document.blocks.size()))
        return;
    const DocBlock& said = document.blocks[number];
    const Block& block = document.doc.block(number);
    if (!CHECK(said.has_spec))
        return;
    const auto same = [&](Scaled came_to, Scaled exact, const char* what) {
        CHECK_CONTEXT("%s", what);
        ++lengths;
        CHECK_EQ(came_to, exact);
    };
    same(block.style.left_indent, in.left_indent, "left indent");
    same(block.space_above, in.space_above, "space above");
    same(block.space_below, in.space_below, "space below");
    if (in.type == BlockType::RULE || hosted) {
        // A host block is, in the block layer, a rule's sort of thing: a
        // height and no material.
        CHECK(block.type == BlockType::RULE);
        same(block.thickness, in.thickness, hosted ? "height" : "thickness");
        return;
    }
    CHECK(block.type == in.type);
    const FontSpec font = blocks_golden::spec_of(in.font);
    CHECK(said.font.face == font.face);
    same(said.font.size, font.size, "size");
    const InterlineSkip& skip = block.style.interline;
    same(skip.baselineskip.width, in.interline.baselineskip.width, "leading");
    same(skip.lineskip.width, in.interline.lineskip.width, "lineskip");
    same(skip.lineskiplimit, in.interline.lineskiplimit, "lineskip limit");
    CHECK(skip.baselineskip == in.interline.baselineskip);
    CHECK(skip.lineskip == in.interline.lineskip);
    same(block.style.strut.height, in.strut.height, "strut height");
    same(block.style.strut.depth, in.strut.depth, "strut depth");
    if (in.type == BlockType::TEXT) {
        CHECK(said.align == in.align);
        same(said.first_line_indent, in.first_line_indent, "first-line indent");
        CHECK(block.style.first_line == FirstLineIndent::BOX_AND_SHAPE);
        CHECK_EQ(block.quote_depth, in.quote_depth);
    }
    same(block.style.marker_gap, theme.marker_gap, "marker gap");
    if (in.has_marker) {
        for (const Node& node : block.text.marker.nodes) {
            if (!node.is(NodeType::CHAR))
                continue;
            FontSpec marker;
            if (!CHECK(parse_font_key(node.font(), &marker)))
                break;
            CHECK(marker.face == in.marker_font.face);
            same(marker.size, in.marker_font.size, "marker size");
            break;
        }
    }
}

// The theme as the document's options came to it.
inline void hold_theme(const ImTypesetDocument& document, const blocks_golden::ThemeIn& theme,
                       int& lengths)
{
    lengths += 2;
    CHECK_EQ(document.doc.theme().quote_indent, theme.theme.quote_indent);
    CHECK_EQ(document.doc.theme().quote_rule_thickness, theme.theme.quote_rule_thickness);
}

} // namespace document_reference
