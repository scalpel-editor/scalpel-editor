# Phase 4 final discoverability observations

Recorded on 2026-07-16 from clean project revision `0eb05e6adbc9e4c63e93f09bace735327f6568ed`. The full matrix contains 240 searches: vector and hybrid, whole repository and `scintilla/src`, normal and held-out. The grepai build is `47bba43`; the runner used one hash-verified snapshot of a durable index with 201 files and 2,150 chunks. `benchmark-results/` is excluded by the checked-in `.grepaiignore`, preventing older query and result JSON from competing with source.

## Acceptance

Under the required vector / whole-repository cell, retained natural-language queries place the expected concern in the top three for 27 of 33 cases (81.8 percent), above the 80 percent gate. All 11 retained held-out concerns occur in the first ten and seven occur in the top three. The exact-name `rg` audit passes every feature; vector and hybrid exact-name ranks are retained as diagnostics under the final two-step rule. Cold navigation reaches every sampled implementation in one or two searches. Deleted `SetTechnology` has no live definition or dispatch case and remains recorded in `MESSAGE_REMOVAL.md`.

| Default retained slice | Top three | Total | Percent |
| --- | ---: | ---: | ---: |
| Exact-name grepai diagnostic | 6 | 11 | 54.5% |
| Spaced, intent, and effect gate | 27 | 33 | 81.8% |
| Held-out diagnostic | 7 | 11 | 63.6% |

The exact semantic misses are `SetReadOnly`, `SetModEventMask`, `SearchInTarget`, `Undo`, and `LineDown`. Intent and effect searches find their actual concern work: read-only intent ranks `EditorDocument.cxx` first, modification-mask spaced/effect rank `EditorHost.cxx` first, target-search effect ranks `EditorSearch.cxx` first, Undo intent ranks `EditorHistory.cxx` first, and LineDown intent ranks the intentionally shared movement helper first. Rearranging correct files around broad identifier queries would contradict the reader-oriented concern rules; `exact-name-rg.md` records the authoritative exact resolution.

## Boundary stability

Temporary comments of about one overlap were inserted immediately before `Editor::SetWrapMode` and the public `Editor::SearchInTarget`, indexed, and measured across all modes and scopes for `SetWrapMode`, `WrapCount`, and `SearchInTarget`. The comments were removed and the original byte-identical files were made durable again. Under vector / whole repository, all wrapping ranks stayed the same or improved. Target-search exact changed from absent to rank 1, spaced stayed rank 2, intent improved from 6 to 2, effect stayed rank 1, and held-out improved from 9 to 3. No selected query regressed; raw results are in `boundary/`.

## Completion evidence

See `exact-name-rg.md`, `cold-navigation.md`, and `completion-audit.md`. The focused editor suite and three-tree check matrix are the remaining Phase 4 phase-gate commands and are recorded in the roadmap completion commit rather than inferred from search results.
