# Phase 5 final discoverability observations

Recorded on 2026-07-16 from clean project revision `03d6703f7268ce885de73b2967187a821a24373e`. The full matrix contains 240 searches: vector and hybrid, whole repository and `scintilla/src`, normal and held-out. The grepai build is `47bba43`; the runner used one hash-verified snapshot of a durable index with 199 files and 1,921 chunks.

## Acceptance

Under the required vector / whole-repository cell, retained natural-language queries place the expected concern in the top three for 28 of 33 cases (84.8 percent), above the 80 percent gate and one result above the Phase 4 final record. The exact-name `rg` audit passes every feature, cold navigation reaches every sampled implementation in one or two searches, and the deleted `SetTechnology` operation has no live definition. The generated client files, numeric dispatch, message constants, coercion helpers, and client packing types are absent.

| Default retained slice | Top three | Total | Percent |
| --- | ---: | ---: | ---: |
| Exact-name grepai diagnostic | 6 | 11 | 54.5% |
| Spaced, intent, and effect gate | 28 | 33 | 84.8% |
| Held-out diagnostic | 6 | 11 | 54.5% |

The exact semantic misses are `SetModEventMask`, `SearchInTarget`, `Undo`, and `LineDown`; `rg` reaches their definitions directly. The five natural-language misses are the broad spaced-name queries for read-only, target search, Undo, and LineDown plus the target-search intent query. The intent and effect queries still reach the actual document, history, movement, and target-search work, and cold navigation reaches target search with one refinement.

All 11 retained held-out concerns occur in the first ten, compared with all 11 and seven top-three results at the Phase 4 gate. Six are now top-three: `SetWrapMode` moved from rank 2 to 6 and `SearchInTarget` from 9 to 9 after the generated type headers were replaced and fixed-window chunk boundaries moved; other held-out ranks partly improved. This is recorded as a diagnostic under the Phase 4 final two-step rule. No generated interface, deleted client header, obsolete HTML, or benchmark output outranks a live concern.

## Completion evidence

See `exact-name-rg.md`, `cold-navigation.md`, and `completion-audit.md`. The focused editor suite and three-tree check matrix are the remaining Phase 5 phase-gate commands and are recorded in the roadmap completion commit rather than inferred from search results.
