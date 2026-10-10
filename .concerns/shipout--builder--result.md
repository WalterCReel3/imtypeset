[from builder] shipout done (filed by lead; summary of the report)

API (ImTypeset::detail): DrawCmd — 64-byte trivially copyable tagged POD
(GLYPH, RULE_RECT, SET_GLUE, ATOM, LINE), Point, Rect, pascal_round, vet_glue,
is_atom, atom_box, and
  ShipError ship(const NodeArena&, const Box&, NodeView contents, Point origin,
                 CharMeasure, std::vector<DrawCmd>& out, uint32_t max_items);
  ShipError ship(const NodeArena&, const Node& box, Point origin, CharMeasure,
                 std::vector<DrawCmd>& out, uint32_t max_items);
ship APPENDS to `out`; indices restart at 0 per call; on error `out` is restored.
A LINE precedes what it covers and its [start, end) is patched after its
contents are walked; an ATOM is followed by its ink, whose glyphs carry the
ATOM's index.

Ran: debug, ASan+UBSan, release — 11 tests / 20,483 checks. All 126 ship + 11
atom cases, 586 assembled paragraphs and 4,397 formula boxes match as text.
20 mutations each fail a test.

HAZARD across modules: a BOX node is an index, so two nodes can name the same
box; a tree doing that at every level is exponential to walk (20 levels x 2
references = 1M visits from 40 arena nodes). ship is bounded by max_items
(ShipError::TOO_MANY_ITEMS). hpack, vpack, deep_equal and NodeArena::import
have NO such bound. For `stress` and for whatever the public API promises about
hand-built node lists.

Citation finding: advtext/shipout.py puts `cur_v := cur_v - height(this_box)`
in §630; tex.web has it in §629.
