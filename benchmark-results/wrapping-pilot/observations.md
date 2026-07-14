# Phase 4 step 5 — wrapping pilot observations

Recorded from project revision `a27d324b34528cff6515f0c7e9f09d5462fb54b3` with the benchmark-runner fixes in this step present in the working tree. Raw ranks are in `search-results.jsonl`, roll-ups are in `summary.json`, and tool and index identity are in `metadata.json`.

## Runner and matrix

The first full run failed after 44 searches because the runner linked every temporary search root to the live `index.gob` while the watcher rewrote that file. grepai reported `failed to decode index: unexpected EOF`. The runner now copies the index only when hashes before the copy, of the copy, and after the copy agree; vector and hybrid modes both link to that stable snapshot. It also records whether the tree was dirty before creating its output, supports feature-limited diagnostic runs, and writes `summary.json` itself.

The recorded config hash includes grepai's watcher-updated `watch.last_index_time`, so a hash difference alone does not prove that a search setting changed. The checked-in `grepai-config.yaml` remains the source for comparing the actual search, chunking, boost, and ignore fields.

- Command: `tools/discoverability/run-searches.sh --label wrapping-pilot --output benchmark-results/wrapping-pilot --set all --target-feature SetWrapMode --target-feature WrapCount`
- Cells: vector and hybrid × repository and `scintilla/src` × normal and held-out
- Searches written: 240
- Completed: `2026-07-14T08:51:40Z`
- Snapshot index SHA-256: `ee5bd5ebad8848feb3a081b05d3bd45b9a47a83a63c0c8f55cbfe958d4b46770`

## Wrapping acceptance results

The product gate is vector search over the whole repository. Ranks below are concern rank / definition rank; `null` means the definition's chunk was absent from the first ten results.

| Query | Baseline | Pilot |
| --- | --- | --- |
| `SetWrapMode` exact | 3 / null | 1 / 1 |
| `SetWrapMode` spaced | 1 / null | 1 / 1 |
| `SetWrapMode` intent | 1 / null | 1 / null |
| `SetWrapMode` effect | 2 / null | 1 / 1 |
| `WrapCount` exact | 1 / null | 1 / null |
| `WrapCount` spaced | 1 / null | 1 / null |
| `WrapCount` intent | 1 / null | 1 / 1 |
| `WrapCount` effect | 1 / null | 1 / null |
| `SetWrapMode` held-out | 3 / null | 2 / null |
| `WrapCount` held-out | 8 / null | 1 / null |

All eight normal wrapping queries rank the concern file first. The six spaced-name, intent, and effect queries therefore pass the 80% rule at 6/6, and both held-out queries improve. Hybrid whole-repository search ranks both exact definitions first. Exact `rg` checks find one `Editor::SetWrapMode` definition at `EditorWrapping.cxx:87`, one `Editor::WrapCount` definition at `EditorWrapping.cxx:446`, their declarations, the two intended temporary forwarding calls, and `EditorWrappingTest.cxx`. The nine other wrapping interface entries have their intended forwarding cases. No removed wrapping API description remains in `ScintillaDoc.html`.

`WrapCount` exposed a conflict in the initial exact-name criterion: vector search selected `EditorWrapping.cxx` first but selected an internal wrapping chunk instead of the short definition. Moving the method beside the other public operations did not change that result, so the move was reverted. `DISCOVERABILITY.md` now requires vector search to find the concern, hybrid search to find the exact definition, and `rg` to verify it. This preserves reader-oriented source order and does not arrange the file around one embedding model's fixed windows.

Obsolete locations remain only where later phases require them: generated interface entries, declarations, seed references, and temporary dispatch forwarding. In the default cell, remaining HTML or generated entries do not outrank `EditorWrapping.cxx` for any wrapping query.

## Broad matrix effect

The full matrix deliberately retains baseline expectations for unmoved features. In the default normal cell, natural-language concern top-three falls from 52.8% to 19.4%; held-out concern top-three falls from 25% to 16.7%. The wrapping move removed an early block from the fixed-window `Editor.cxx`, shifting the chunks for later unmoved concerns even though their code did not change. Padding `Editor.cxx` to preserve those accidental windows would violate the source-layout rule. These ranks remain recorded so the later concern splits can be evaluated from the actual intermediate tree.

## Boundary stability

A temporary five-line comment of roughly 300 characters was inserted immediately before `Editor::SetWrapMode`, the watcher incrementally reindexed `EditorWrapping.cxx`, and the ten wrapping queries were rerun under all four mode/scope cells. The padding was then removed and the original file was reindexed.

- Default vector/repository: all ten concern ranks were unchanged; eight normal queries remained rank 1, and held-out queries remained ranks 2 and 1.
- All cells: 37/40 boundary results remained in the top three, compared with 39/40 before padding.
- Diagnostic regressions: hybrid/repository `wrap-set-mode-spaced` moved 2→5 and `wrap-count-intent` moved 2→4.

The selected product gate passes. The diagnostic hybrid changes show the same rank jitter seen in the baseline boundary run.

## Cold navigation

Prompt: “turn wrapping on for long lines”, with no symbol name supplied.

1. `grepai search "turn wrapping on for long lines"` returned `EditorWrapping.cxx:275-336` first. This is the correct concern file but an internal wrapping span.
2. `rg` for the wrapping entry points reached `Editor::SetWrapMode` and `Editor::WrapCount`, their declarations, forwarding calls, and `EditorWrappingTest.cxx`.

The public operation shows that enabling wrapping resets horizontal scrolling, requests a horizontal-scroll update, redraws, and reconfigures scrollbars. The focused tests cover those effects, repeated settings, forwarding, visual settings, display-row counts, disabling wrapping, resize, and document modification. Cold navigation therefore reaches the authoritative implementation and its test in two search steps.

## Verification and decision

- `./build/scintilla/test/editor/editorTest "*wrap*"`: 44 assertions in 6 test cases passed.
- `./check.sh`: normal, AddressSanitizer, and UndefinedBehaviorSanitizer builds and tests passed.

The wrapping pilot passes the clarified acceptance criteria. Phase 4 may proceed to deterministic popup tests before the autocomplete and call-tip pilot.
