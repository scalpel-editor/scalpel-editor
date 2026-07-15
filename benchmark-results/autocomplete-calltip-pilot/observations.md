# Phase 4 step 8 — autocomplete and call-tip pilot observations

Recorded from project revision `a6fcdf8b91c55ed6d26e0f4ba075523a9bb794c3` with the source-order corrections from this step present in the working tree. Raw ranks are in `search-results.jsonl`, roll-ups are in `summary.json`, and tool and index identity are in `metadata.json`.

## Matrix

- Command: `tools/discoverability/run-searches.sh --label autocomplete-calltip-pilot --output benchmark-results/autocomplete-calltip-pilot --set all --target-feature AutoCShow --target-feature CallTipShow`
- Cells: vector and hybrid × repository and `scintilla/src` × normal and held-out
- Searches written: 240
- Completed: `2026-07-15T03:43:25Z`
- Snapshot index SHA-256: `54d0d6fa6f17268f1c4557acfc24294e522fc01e1bf44e4ec8ebb5b2dc0ed62e`

## Pilot correction

The first recorded run showed that the move alone did not meet the acceptance criteria. `AutoCShow` followed roughly 260 lines of private completion work: its spaced query ranked the concern file fifth, its effect query ranked it eighth, and the held-out query did not find it. Moving the named autocomplete operations before the private workflow made all four normal queries and the held-out query rank `EditorAutocomplete.cxx` first without changing names or benchmark wording.

Moving the whole call-tip public block to the front was the wrong rule: normal queries stayed useful, but the held-out query regressed from rank 6 to outside the first ten. The accepted layout instead keeps the public `CallTipShow(position, definition)` operation directly beside the point-based placement helper it calls. The public operation now directly cancels autocomplete, and the existing description of that effect moved with the behavior. The helper only places and draws the tip. This layout keeps all normal queries in the top three, improves the held-out query, and remains stable under the boundary shift.

## Acceptance results

The product gate is vector search over the whole repository. Ranks below are concern rank / definition rank; `null` means the definition chunk was absent from the first ten results.

| Query | Baseline | Pilot |
| --- | --- | --- |
| `AutoCShow` exact | 2 / 2 | 1 / 1 |
| `AutoCShow` spaced | 3 / 3 | 1 / null |
| `AutoCShow` intent | 5 / null | 1 / null |
| `AutoCShow` effect | 10 / null | 1 / null |
| `CallTipShow` exact | 5 / 5 | 3 / 3 |
| `CallTipShow` spaced | 5 / 5 | 3 / 3 |
| `CallTipShow` intent | 4 / null | 3 / 3 |
| `CallTipShow` effect | 4 / null | 1 / 1 |
| `AutoCShow` held-out | null / null | 1 / null |
| `CallTipShow` held-out | 9 / null | 6 / null |

All six spaced-name, intent, and effect queries place the concern file in the top three, for 100% against the 80% requirement. Both exact queries place the concern and authoritative definition in the top three under vector and hybrid repository search. Both held-out queries improve over baseline. Generated interfaces, declarations, and temporary forwarding paths remain during the message-removal transition; none outranks the concern for autocomplete, and the call-tip effect deliberately finds the temporary mixed forwarding chunk alongside the authoritative concern because that chunk contains both popup forwarding cases.

Exact `rg` checks find one public `ScintillaBase::AutoCShow` definition, one public `ScintillaBase::CallTipShow` definition, the private point-based call-tip helper, their declarations, the two intended temporary forwarding cases, and the focused tests. The Autocompletion, User lists, and Call tips sections of `ScintillaDoc.html` contain only pointers to the concern files; retained notification descriptions remain live documentation.

The full-corpus summary continues to use baseline expectations for features that have not reached their Phase 4 target files, and this run does not retarget the wrapping rows. Its aggregate percentages are diagnostic only; the acceptance decision above counts the two features moved by this pilot.

## Boundary stability

Each concern was shifted independently by a temporary four-line comment of roughly 280 characters immediately before its public entry point. The watcher indexed the padded file, the feature's 20 searches ran under both modes and scopes, the padding was removed, and the original file was reindexed before the other concern was changed. Raw results are in `boundary/autocomplete/` and `boundary/call-tip/`.

- Autocomplete default vector/repository ranks after the shift: exact 3, spaced 1, intent 1, effect 1, held-out 1.
- Call-tip default vector/repository ranks after the shift: exact 3, spaced 3, intent 3, effect 3, held-out 6.
- Every normal query remains in the top three. The held-out ranks do not regress: autocomplete stays at 1 and call tips stay at 6.

The foreground watcher exposed a tooling deficiency during these runs. `grepai status` reported the watcher running and advanced the index time after each file modification, but the reported log file still ended with an older stopped process and did not receive the required `Indexed <file>` lines. Each run therefore also used the runner's hash-verified snapshot and a smoke search that returned the changed line spans before recording results. A later retest after the grepai fix created and removed a uniquely named Markdown probe: the reported log recorded queued, indexed, removed, steady, and durable-save events; durable generations advanced for addition and removal; the indexed file and chunk counts returned to their starting values; and search no longer returned the removed file. `grepai status --wait --steady --indexed <path> --after <mtime>` also waited through the in-memory update until the changed GOB snapshot was durable. The foreground log and synchronization problems are fixed; future benchmark runs should use the status wait command rather than sleeps or log scraping. This paragraph retains the limitation that applied while the recorded benchmark ran.

## Cold navigation

Autocomplete prompt: “offer completion choices after a prefix.” Vector repository search returns `EditorAutocomplete.cxx` first. An exact `rg` search from the named operations visible in that file finds `AutoCShow`, its declaration and forwarding case, and `EditorAutocompleteTest.cxx`. The public operation shows list parsing, call-tip cancellation, and choose-single behavior through its nearby description and direct call into the private workflow.

Call-tip prompt: “open signature help beside the insertion point.” The first search returns four unrelated core files, then the lower-level `CallTip.h`, and `EditorCallTips.cxx` at rank 6. Inspecting the feature-named files identifies the lower-level drawing type and the editor operation; one exact `rg` search then finds the public and private overloads, forwarding case, and `EditorCallTipsTest.cxx`. Both checks reach the authoritative implementation and focused tests in no more than two search steps.

## Verification and decision

- Focused autocomplete-related filters: 111 assertions in 14 test cases passed.
- Focused call-tip filters: 57 assertions in 8 test cases passed.
- `./check.sh`: normal, AddressSanitizer, and UndefinedBehaviorSanitizer builds and tests passed.

The second pilot passes. The concern-file rule is approved for the broad Phase 4 split: keep authoritative behavior descriptions beside named definitions, keep the public operation's important effects in its direct path, and choose public/private order from the concern's readable control flow rather than imposing one fixed order. Phase 4 may proceed to the remaining-inventory classification.
