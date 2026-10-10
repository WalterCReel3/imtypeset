[from reviewer-3] Review of the layout core (filed and condensed by lead). All READ, nothing run,
unless marked. Owners: `stress` fixes A-F (and for that purpose also owns src/nodes.h, src/nodes.cpp,
tests/test_nodes.cpp); `tex-web-pass` takes G-J.

No layout divergence from the Python was found. The merge-of-two-runs formulation in try_break is
equivalent to the reference's stable sort (both runs are provably sorted). Hazard items all honoured.

A. UB — a box can contain itself. NodeArena::nesting answers 0 for a BOX/DISC index >= the current
   count and add_box never inspects content indices, so:
       NodeArena arena;
       Node self; self.type = NodeType::BOX; self.u = (uint32_t)arena.box_count();
       const std::vector<Node> inside = {self};
       const Node box = arena.add_box(Box{}, inside);   // no contract fires
       std::vector<DrawCmd> out;
       ship(arena, box, Point{}, CharMeasure(), out);   // recursion until the stack ends
   deep_equal and import recurse the same way with no budget. Same hole through push_disc and through
   a stale node kept across rewind(). Fix in add_box/push_disc: a content BOX with u >= _boxes.size()
   (or DISC with u >= _discs.size()) is a malformed list -> store with empty contents, as the too-deep
   case does. tests/test_nodes.cpp `nesting_is_refused_past_the_limit_and_never_cycles` does not test
   cycles. Confirm: the snippet under asan with a ContractHandler installed.
B. ship's default bound does not protect: MAX_SHIP_ITEMS = 0xFFFFFFFF, so a 32-level shared-box tree
   pushes 128-256 GB of DrawCmd before TOO_MANY_ITEMS; under -fno-exceptions that is an abort. Tie the
   default to arena.node_count() + contents.size(). The default path is untested.
C. NodeArena::import and deep_equal have no bound and walk every path of a shared tree; import
   un-shares it into 2^n nodes. import should memoise per source box/disc index (which also preserves
   sharing); deep_equal needs a budget or an "already equal" memo. hpack/vpack do NOT recurse.
D. When MAX_DIMEN is not honoured it is signed-overflow UB (not wrap, not clamp) in hpack/vpack/
   set_glue, GlueTotal::add, shipout's cur_h/cur_v, ParShape::hanging, and whatever CharMeasure
   returns. Nothing establishes the invariant yet: public-api needs ONE validating entry (clamp every
   dimension of a host-supplied list, CharDimens in the measure adapter, LineSpec). Stress case:
   INT64_MAX widths under UBSan.
E. Total fit has no work budget (cost INFERRED): without easy_line the active list grows with line
   count whenever every line is feasible (fil right_skip, infinite interword stretch, tolerance 10000).
   Estimated >= n^2. _passive is indexed by int32. Recommend a cap on actives/evaluations with a stated
   fallback to first-fit and a flag saying it happened. Confirm: the 635-node sample x16 and x64,
   right_skip 0pt plus 1fil, first_fit false.
F. A bad box index is answered through IMTYPESET_CONTRACT (assert) although a hand-built list is data
   (D17); in release it silently packs/ships as an empty box with error NONE. ship(arena, Node, ...)
   on a non-BOX root returns ShipError::NONE.

G. Citations: ~45 checked by counting section starts in tex.web; ONE wrong — src/pack.cpp set_glue
   cites §659 for the zero-stretch case, it is §658 (inherited from pack.py).
   Imprecise: build_line cites only §881 (prune 879; disc 882-885; right skip 886; left skip 887);
   find_breakpoints cites only §866 (glue 868, disc 869); hpack CHAR arm 654, glue 656; vpack glue 671;
   PackMode uncited (644); classify omits that §852's 7230584/1663497 guard is not reproduced.
H. Departure not stated: src/linebreak.h says which breaking wins does not depend on easy_line. True
   of the minimum, NOT of ties: tex.web §845 links a new active before the first node of the next
   class (newest first within a class) and with a uniform shape all line numbers are one class; the
   reference keeps each line count as its own class and sorts survivors first, so an exact tie can
   resolve the other way. docs/divergences.md has no entry.
I. Names TeX has and the port (after the Python) spells otherwise: MATH_ON/MATH_OFF = math_node
   before/after (§147); RUNNING = null_flag (§138); break_paragraph = line_break (§815); assemble +
   build_line = post_line_break (§877); Passive.position/previous = cur_break/prev_break (§821);
   Active.passive = break_node (§819); best_demerits = minimal_demerits, minimum = minimum_demerits
   (§833); artificial = artificial_demerits (§854); penalty_value = pi; hyphenated = break_type;
   Fitness members = very_loose_fit..tight_fit (§817); vpack with max_depth = vpackage (§668);
   PackResult.badness = last_badness (§646); pack.cpp's set_glue has no TeX name and collides with
   DrawCmd::set_glue. At minimum put the TeX name in the comment at each.
J. Verbatim tex.web text in sources, one line each: linebreak.cpp SATURATED_DEMERITS comment (line
   16902) and is_prunable (line 16513); shipout.cpp three Pascal statements and confusion("vlistout");
   nodes.h GlueTotal reproduces §646's worked example and "essentially 2fil".
K. Tests: core_golden::token(GlueSign/KernKind) indexes with % 3 and GlueOrder with & 3, so a corrupt
   enum prints as a legal token.
