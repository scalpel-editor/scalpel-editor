# scalpel-editor discoverability case study

This document records the project-specific evidence and decisions behind the reusable [discoverability lessons](DISCOVERABILITY_LESSONS.md). The live benchmark procedure and acceptance criteria remain in [DISCOVERABILITY.md](DISCOVERABILITY.md). Raw measurements and navigation notes remain under [`benchmark-results/`](benchmark-results/).

## Scope

Phase 4 moved editor features from the roughly 9,000-line pre-split `Editor.cxx` and `ScintillaBase.cxx` into named concern files while replacing numeric dispatch with direct operations. Phase 5 then deleted the generated message and client layers. The benchmark followed wrapping, scrolling, document state, notification policy, target search, commands, popups, printing, and the deleted `SetTechnology` renderer setting across exact-name, descriptive, effect, and held-out queries.

The required search cell was vector search over the whole repository. Hybrid and source-limited searches were recorded as diagnostics. Exact definitions and callers were checked with `rg`, and cold navigation began from a descriptive prompt without revealing the symbol name.

## Pilot results

The wrapping baseline left named operations inside `Editor.cxx`. Moving the complete wrapping concern into `EditorWrapping.cxx` improved the feature's results without making every short definition the first returned chunk.

| Check | Baseline | Wrapping pilot |
| --- | --- | --- |
| Normal wrapping queries with concern in top three | 8/8, ranks 1 to 3 | 8/8, all rank 1 |
| `SetWrapMode` exact definition rank, vector over repository | absent from top 10 | rank 1 |
| `WrapCount` exact definition rank, vector over repository | absent from top 10 | absent from top 10; concern rank 1 |
| `SetWrapMode` held-out concern rank | 3 | 2 |
| `WrapCount` held-out concern rank | 8 | 1 |

The broad corpus regressed for concerns that had not moved: natural-language top-three results fell from 52.8 percent to 19.4 percent, and held-out top-three results fell from 25 percent to 16.7 percent. Removing an early block shifted later fixed windows in `Editor.cxx`; the unchanged code was not treated as a refactor failure.

The autocomplete and call-tip pilot tested a different owner and popup state. All six spaced-name, intent, and effect queries placed their concern in the top three, and both held-out queries improved over baseline. The pilot rejected a universal public-first order: autocomplete benefited from named operations before a long private workflow, while call tips worked best with the public show operation beside the placement helper it calls. This approved concern-oriented organization for the rest of Phase 4.

Detailed pilot evidence is in the [wrapping observations](benchmark-results/wrapping-pilot/observations.md) and [autocomplete/call-tip observations](benchmark-results/autocomplete-calltip-pilot/observations.md).

## Final measurements

Both final gates used the unchanged query text, validated final-tree expectations, a hash-verified durable index snapshot, and a 240-search matrix.

| Default retained slice | Phase 4 final | Phase 5 final |
| --- | ---: | ---: |
| Exact-name grepai top three, diagnostic | 6/11 (54.5%) | 6/11 (54.5%) |
| Spaced, intent, and effect top three, gate | 27/33 (81.8%) | 28/33 (84.8%) |
| Held-out top three, diagnostic | 7/11 (63.6%) | 6/11 (54.5%) |
| Cold navigation | every sample within two searches | every sample within two searches |
| Deleted `SetTechnology` | no live operation; decision record found | no live operation; decision record found |

Phase 5 removed generated interfaces, message dispatch, client packing types, and obsolete HTML. Natural-language concern results improved by one, exact-name results stayed unchanged, and held-out top-three results fell by one. The mixed result is why rank changes alone do not establish whether removing repository noise improved navigation. No deleted generated interface, client header, obsolete HTML, or benchmark output outranked a live concern in the final record.

The authoritative records are the [Phase 4 final observations](benchmark-results/phase4-final/observations.md) and [Phase 5 final observations](benchmark-results/phase5-final/observations.md).

## Corpus corrections

The first final-tree audit found valid-looking expectations for files that never existed. `SetModEventMask` was planned for `EditorNotifications.cxx` and `EditorNotificationsTest.cxx` but was deliberately implemented in `EditorHost.cxx` and tested in `EditorHostTest.cxx`. The command evidence named `EditorCommandsTest.cxx` instead of the actual `EditorCommandTest.cxx`. The deleted renderer setting named a test file that was never created. `tools/discoverability/check-corpus.sh` now requires unique IDs, valid kinds and dispositions, existing target and evidence files, and a definition marker in every target.

Expectations also became query-specific. `Undo` command lookup targets `EditorCommands.cxx`, while intent, effect, and held-out queries target `EditorHistory.cxx`. `LineDown` command lookup targets `EditorCommands.cxx`, while behavior queries target the shared movement work deliberately retained in `Editor.cxx`. The query text did not change; [DISCOVERABILITY.md](DISCOVERABILITY.md) records the ownership corrections and points to the uncorrected step 19 result.

Retained and deleted features are scored separately. The 11 retained features contribute to concern-rank percentages. `SetTechnology` passes only when its implementation is absent and its decision in `MESSAGE_REMOVAL.md` remains findable.

The project also excludes `benchmark-results/` through `.grepaiignore`. Earlier JSON records repeat queries, target files, and definition markers, so allowing them into the index made each recorded run compete with later source searches.

## Phase 5 workflow findings

The completion script originally rejected unrelated names such as `ScopedMessage` and the retained Lexilla `SCI_METHOD` calling-convention macro while missing deleted client packing types such as `CharacterRange*`, `TextRange*`, `TextToFind*`, and `RangeToFormat*`. Its final allowlist records exact token/path pairs with owners and reasons; it does not exclude whole files. The experience also established a stronger test rule: legitimate external spellings should pass and representative forbidden forms should fail, so a clean current tree is not the only evidence that an absence gate works.

The final audit order became: validate the corpus, run completion scans, audit exact names, perform cold navigation, correct real gaps, wait for the durable index, then record the full benchmark snapshot. Exact searches found the missing packing-type checks, and cold reading found target-search comments and state detached from the operation in `EditorSearch.cxx`, before the final matrix was recorded.

The search correction kept target endpoints, direction, operation, and focused behavior contiguous in `EditorSearch.cxx`. Live source comments now state the current contract directly instead of repeating deleted names merely to say they no longer exist.

All 91 C++ source and header files changed during Phase 5 required separate `grepai status --wait --indexed` calls. This exposed the need for a batch durability barrier. Sandboxed waiting also sometimes tried to open the watcher lock in a read-only location even though the watcher snapshot was readable, so cross-namespace watcher status is improved but not fully read-only.

A local Ollama access failure left a partial benchmark directory. The runner still needs provider preflight and transactional output: create temporary results only after access succeeds, then rename them into the requested path after searches and summaries complete.

## Current project decisions

- Organize `Editor` and `ScintillaBase` implementation by coherent editor concerns and keep related state, policy, operations, and focused tests together.
- Move complete concerns rather than isolated wrappers, and keep authoritative descriptions beside named definitions.
- Use descriptive vector search to locate a concern and `rg` or structural search to resolve exact definitions and callers.
- Keep query text fixed, validate target expectations as data, and assign destinations per query.
- Score retained and deleted features separately and exclude benchmark output from the search index.
- Record boundary and held-out movement as diagnostics; do not arrange source around one embedding model's fixed windows.
- Run completion and exact-source audits before expensive benchmark snapshots, then verify behavior independently with focused tests and the full build matrix.
