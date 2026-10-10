[from builder] selection done (filed and condensed by lead). src/selection.{h,cpp},
tests/test_selection.cpp, tools/oracle/dump_selection.py, tests/golden/selection (57 cases), D29.
10 tests / 193,825 checks in debug, ASan+UBSan, release; 600 fuzz buffers agree with a plain scan.

API (ImTypeset::detail): Selection{start,end}, Granularity, Fragment, TextData/IndexedText,
SelectionModel { build(cmds,count); length(); fragment_at(Point); hit(Point); select(a,b);
select_at(Point,Point,Granularity); word_at; line_at; rects(Selection, std::vector<Rect>& out);
text(Selection, const TextData&, std::string& out) }. Point queries allocate nothing.

For public-api / api-math:
1. text() takes separators / hard_breaks / atoms / originals as arrays SORTED ascending by index,
   unique. public-api supplies "\n\n" at paragraph starts; api-math the atom index -> "$source$" map.
2. Indices are uint32 positions in ONE ship() call's sequence; ship() restarts at 0 per call.
   LEAD RULING: one SelectionModel per shipped paragraph (as the Python views do), and the public
   API exposes a document-level position as (paragraph, index) or a paragraph-base offset — never a
   bare index that collides across paragraphs.
3. rects() on a line whose item indices do not ascend: the reference raises, the port gives that
   line no rectangle. Ship-out never produces such a line.

Consolidation (LEAD RULING, for `stress`, which owns src/selection.cpp): replace the Unicode tables
embedded in selection.cpp with src/unicode_tables.h so there is one copy.
