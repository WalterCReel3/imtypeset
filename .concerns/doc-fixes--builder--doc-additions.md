# doc-fixes: text for the shared documents (for the `docs` task)

`docs/decisions.md` was edited directly (D45 appended; two one-line pointers added inside D43,
at the `DocumentHook` bullet and the `DescriptionCut` bullet). Nothing below is applied yet.

## docs/divergences.md, section `## api-document`, first paragraph

The sentence beginning "Under the test-only `DocumentHook` (reference `DocumentRules`, reference
`TexRules`, exact scaled points) the layout is the reference's:" is no longer true as written.
Replace that clause with:

> Under the reference's rules (`ImTypesetContext::tex_rules` and
> `ImTypesetDocument::reference_rules`, internal state a test sets; no public call selects
> either) the layout is the reference's, with every length handed over as float pixels through
> the public structs and held to the golden's scaled points by `==` (`tests/document_reference.h`;
> `docs/decisions.md` D45):

The counts that follow in that paragraph (67 of 69 block goldens, 211 compositions, 34,647
commands; the 22 layouts of `tests/golden/doc`) were re-measured after the change and stand.

## README.md / docs/spec-status.md, wherever the Document's error model is described

- A run out of order (a paragraph's or a block's) is `ImTypesetError_DescriptionCut`, a "done,
  differently" code: the text is set and the run is left out. It was `InvalidArgument`.
- `AppendDocumentText` answers for that call alone; `GetDocumentBlockError` is where a block's
  standing trouble is read.
- `DocumentView` with a retained selection: `ReplaceDocumentBlocks` moves it with its blocks.
- Test counts, if quoted: `test_api_document` 24 tests, `test_api_document_golden` 5,
  `test_exception_safety` 9 (two of them the Document's), `test_c_api` 464 checks.

## CLAUDE.md, "Working alongside others" (a suggestion, the `docs` task's to take or leave)

A hazard met in this task and not written down anywhere: when another agent saves a header
while your private build is compiling, ninja can finish an object whose compile STARTED before
the save and ENDED after it. The object is then newer than the header and is never rebuilt, and
the link mixes two layouts of the same struct (here `Drawer`, inside `ImTypesetContext`: a
segfault in an unrelated function). `ninja -C build/<task> -t clean imtypeset` and a rebuild
cures it; a crash that makes no sense right after someone else's header changed is this first.
