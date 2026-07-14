# Phase 4 step 2 — current-tree baseline observations

Recorded from project revision `7fe152d41171ee67c1263abbeebef72a3d56e214` (phase 4 step 1 corpus commit). Raw search ranks are in `search-results.jsonl`; roll-up counts are in `summary.json`. Tool and index identity are in `metadata.json`.

The query corpus in `tools/discoverability/queries.tsv` and `held-out-queries.tsv` is fixed after this baseline. Change a corpus row only when `DISCOVERABILITY.md` records the reason and the old and new evidence.

## Index and configuration

| Field | Value |
| --- | --- |
| Index prep | Full wipe and re-embed (14m20s). Not shown to be necessary; later runs should use incremental reindex of changed files only (see `DISCOVERABILITY.md`). |
| Files / chunks | 147 files, 2079 chunks |
| Index SHA-256 | `a8992c463e8493f1a9017c552cc66be9ef59203773e88e5feee6ed82ca21925b` |
| grepai | `dev-iface`, binary SHA-256 `9a7505bf…eb1d` |
| Source | `/my/src/grepai` branch `iface` at `f1149ad` (dirty: local `.iface` scanner work) |
| Embedder | ollama `nomic-embed-text`, 768 dims |
| Chunking | size 512, overlap 50 |
| Hybrid in live config | disabled (`search.hybrid.enabled: false`) |
| Runner hybrid cell | temporary config copy with hybrid enabled; same frozen index |

Default product configuration for acceptance comparison is **vector / repository** (whole-tree search with hybrid off).

## Matrix run

- Command: `tools/discoverability/run-searches.sh --label baseline --output benchmark-results/baseline --set all`
- Cells: vector and hybrid × repository and `scintilla/src` × normal and held-out
- Searches written: 240
- Completed: `2026-07-14T04:41:13Z`

## Exact-name `rg` check

Every exact-name corpus query finds its recorded baseline definition text in the baseline file:

| Query id | Definition text | Location |
| --- | --- | --- |
| wrap-set-mode-exact | `Editor::SetWrapMode` | `Editor.cxx:1509` |
| wrap-count-exact | `Editor::WrapCount` | `Editor.cxx:6010` |
| scroll-width-exact | `case Message::SetScrollWidth` | `Editor.cxx:7317` |
| read-only-exact | `case Message::SetReadOnly` | `Editor.cxx:6629` |
| mod-event-mask-exact | `case Message::SetModEventMask` | `Editor.cxx:8483` |
| search-target-exact | `Editor::SearchInTarget` | `Editor.cxx:4419` |
| undo-exact | `case Message::Undo` | `Editor.cxx:6394` |
| line-down-exact | `case Message::LineDown` | `Editor.cxx:3024` |
| autocomplete-show-exact | `case Message::AutoCShow` | `ScintillaBase.cxx:841` |
| call-tip-show-exact | `ScintillaBase::CallTipShow` | `ScintillaBase.cxx:516` |
| format-range-exact | `Editor::FormatRange` | `Editor.cxx:1981` |
| technology-exact | `case Message::SetTechnology` | `Editor.cxx:9135` |

## Acceptance snapshot against initial criteria

Criteria are from `DISCOVERABILITY.md`. Baseline is a measurement, not a pass gate for step 2; pilots must improve on these numbers.

| Criterion | Default cell (vector / repository / normal) | Notes |
| --- | --- | --- |
| Exact-name `rg` finds definition | Pass (12/12) | See table above |
| Exact-name grepai: definition in top 3 | Fail (1/12 definition_rank ≤ 3) | Concern file top-3 only 5/12; most hits are other chunks of the same huge file or seed/docs |
| ≥80% natural-language/effect: concern in top 3 | Fail (19/36 ≈ 52.8%) | Held-out same cell: 3/12 ≈ 25% |
| Cold nav ≤ 2 search steps | Mixed — see below | Wrap passes; autocomplete borderline; search needs ignoring docs; undo fails |
| Boundary-stability: top-3 concern retained | Pass on default config for wrap queries | See boundary section; two hybrid/source cells moved out of top 3 |
| Obsolete material not outranking | Fail today | HTML and seed often rank above live code on whole-repository search |
| Focused tests + `./check.sh` | Not part of this measurement | No compiled code changed in step 2 |

### Stronger cells (for context, not the default)

- **hybrid / source / normal**: concern top-3 41/48 (85.4%); natural-language concern top-3 30/36 (83.3%). Path limit plus hybrid already meets the 80% concern rule for this tree.
- **vector / source / normal**: concern top-3 29/48 (60.4%) — better than whole-repository vector, still short of 80%.

### Definition rank vs concern rank

`definition_rank` is often `null` even when `concern_rank` is 1–3. Typical cause: the first `Editor.cxx` hit is a neighboring wrap/layout/dispatch chunk that does not contain the recorded definition string (for example `SetWrapMode` query returns lines ~4814 or ~1638 rather than 1509). This matches the phase 2 wrap-mode case study: a short named method is drowned by unrelated neighbors in a multi-thousand-line file.

## Per-feature ranks (vector / repository / normal)

Columns are concern_rank / definition_rank (`null` = absent from top 10).

| Feature | exact | spaced | intent | effect |
| --- | --- | --- | --- | --- |
| SetWrapMode | 3 / null | 1 / null | 1 / null | 2 / null |
| WrapCount | 1 / null | 1 / null | 1 / null | 1 / null |
| SetScrollWidth | 2 / null | 4 / null | 2 / null | 2 / null |
| SetReadOnly | 6 / null | null | 3 / null | 2 / null |
| SetModEventMask | null | 9 / null | 3 / null | 2 / null |
| SearchInTarget | 7 / null | 3 / null | 3 / null | 5 / null |
| Undo | null | null | null | null |
| LineDown | 5 / null | 3 / null | 2 / null | 3 / null |
| AutoCShow | 2 / 2 | 3 / 3 | 5 / null | 10 / null |
| CallTipShow | 5 / 5 | 5 / 5 | 4 / null | 4 / null |
| FormatRange | 1 / null | 5 / null | 7 / null | null |
| SetTechnology | null | null | null | 3 / null |

Notable failures under the default cell: entire **Undo** concern absent from top 10; several exact identifiers lose to seed or documentation; **SetTechnology** is hard to find by name in vector whole-repository search.

## Boundary-stability check

Procedure:

1. Inserted a 277-character three-line comment immediately before `void Editor::SetWrapMode` in `Editor.cxx`.
2. Rebuilt the affected index entries (incremental watch scan).
3. Re-ran all SetWrapMode and WrapCount corpus queries (normal + held-out) under all four mode/scope cells → `boundary/search-results.jsonl` (40 rows).
4. Removed the padding and restored `Editor.cxx` from git (Python rewrite had also stripped CRLF; `git checkout -- scintilla/src/Editor.cxx` restored the file).

Result summary (concern_rank top-3 stability):

- 40 cells compared; 30 remained stable in top 3 when already top 3; 2 regressions; several improvements or stable-out-of-top-3.
- **Default config (vector / repository)**: all SetWrapMode and WrapCount normal queries stayed in top 3; held-out wrap-set-mode improved 3→1; wrap-count held-out stayed out of top 3 (8→6).
- **vector / source**: same pattern — no top-3 regressions for wrap normal queries.
- Regressions were both hybrid/source: `wrap-count-intent` 2→4, `wrap-set-mode-spaced` 2→4.

Interpretation: under the default vector configuration, a ~200+ character boundary shift at `SetWrapMode` did not push the wrapping concern out of the top three for the normal wrap queries. Hybrid path-limited results showed more rank jitter. Padding was not left in the tree.

## Cold-navigation checks

Reader/agent protocol: only the natural-language intent is given first (no symbol name). Default `grepai search` (vector, whole repository). Then exact or path-limited follow-up is allowed as a second search step. Record wrong files opened and whether the authoritative implementation is reached within two search steps.

### A — “turn wrapping on for long lines”

| Step | Action | Result |
| --- | --- | --- |
| 1 | `grepai search "turn wrapping on for long lines"` | Rank 1: `Editor.cxx:1638-1701` (wrap-layout helper, not `SetWrapMode`); rank 2: `ScintillaDoc.html`; rank 3: `EditView.cxx` |
| 2 | In `Editor.cxx`, `rg SetWrapMode` / read nearby wrap API | Definition at `Editor.cxx:1509`; declaration `Editor.h:712`; call from dispatch `Editor.cxx:7248`; tests in `TestEditorTest.cxx` |

**Outcome:** Reached authoritative implementation in two steps. First hit was the correct concern file but the wrong span inside the mixed-purpose file. Focused tests exist for wrap mode side effects.

### B — “show an autocomplete list at the caret”

| Step | Action | Result |
| --- | --- | --- |
| 1 | `grepai search "show an autocomplete list at the caret"` | Rank 1–2: `AutoComplete.h` / `.cxx` (list-box helper); rank 3: HTML; rank 5: `ScintillaBase.cxx:428-502` (near popup path) |
| 2 | Open `ScintillaBase` / `rg AutoCShow` | Dispatch `case Message::AutoCShow` at 841 → `AutoCompleteStart`; no focused `AutoCShow` editor test yet (only includes / platform stub notes) |

**Outcome:** Borderline pass if the reader inspects the top five of step 1 or uses a second search aimed at the show entry. Easy to stop early in the helper type. Application entry is still message-dispatch shaped.

### C — “search within the current target range”

| Step | Action | Result |
| --- | --- | --- |
| 1 | `grepai search "search within the current target range"` | Rank 1: `ScintillaDoc.html` (SCI_SEARCHINTARGET prose); rank 3: `Editor.cxx:4316-4375` (near search code, not the definition line) |
| 2 | `rg SearchInTarget` in `Editor.cxx` / headers | Definition `Editor.cxx:4419`; dispatch 6567; declaration `Editor.h:532`; no dedicated editor test file for this feature yet |

**Outcome:** Fail if the reader trusts rank 1 only (obsolete documentation). Pass if they continue to rank 3 or run a second exact search. Documents the acceptance risk that HTML outranks live code on whole-repository search.

### D — “undo the most recent document change”

| Step | Action | Result |
| --- | --- | --- |
| 1 | `grepai search "undo the most recent document change"` | Rank 1: HTML selection-history prose; ranks 2–5: `UndoHistory` / `Document` / `ChangeHistory` — not `Editor::Undo` |
| 2 | `rg` for undo handling in `Editor.cxx` | `Editor::Undo` at 2487; `case Message::Undo` at 6394 — neither appeared in the first five semantic hits |

**Outcome:** Fail within two search steps from natural language alone. Matches the matrix: all four Undo normal queries miss `Editor.cxx` in the default vector/repository cell.

## Implications for the pilots

1. **Wrapping pilot** starts from a relatively strong natural-language baseline (concern often top 3) but weak definition localization inside `Editor.cxx`. Moving the full wrap concern into a named file is the main expected gain for definition rank and cold navigation precision.
2. **Autocomplete / call-tip pilot** must lift application entry points above helper types and HTML; cold nav currently lands on `AutoComplete` support code first.
3. Whole-repository noise from `ScintillaDoc.html` and `seed/` is large enough that path-limited scores look much healthier than the default experience. Pilot success must be judged on the default cell, not only on `--path scintilla/src`.
4. **Undo** and other command-shaped dispatch cases are nearly invisible to vector search today; the dedicated command type and concern split in later steps are required, not optional polish.
5. Hybrid search helps exact identifiers and some path-limited NL queries but is not the live default; do not tune pilots only against hybrid.

## Corpus freeze

After this baseline:

- Do not edit `tools/discoverability/queries.tsv` or `held-out-queries.tsv` while choosing names, file boundaries, or comments for the pilots.
- Held-out paraphrases remain evaluation-only.
- Target file names already listed in the corpus (`EditorWrapping.cxx`, `EditorAutocomplete.cxx`, and so on) stay as the fixed expected post-pilot locations unless `DISCOVERABILITY.md` records an evidence-based change.
