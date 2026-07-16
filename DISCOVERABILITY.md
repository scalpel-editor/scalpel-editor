# Source discoverability guide

This guide defines how the Scintilla refactor should make features easier to find and understand with exact search, semantic code search, structural tools, and direct source reading. Search quality is part of the refactor result, but no single tool or query defines success.

The accumulated repository-structure lessons and grepai improvement ideas are recorded in [DISCOVERABILITY_LESSONS.md](DISCOVERABILITY_LESSONS.md). This guide remains the source for refactor rules, benchmark procedure, and acceptance criteria.

## What the wrap-mode case showed

The phase 2 `SetWrapMode` extraction gave the operation a direct name and focused behavior tests, but it left the small implementation inside the roughly 9,000-line pre-split `Editor.cxx`. A grepai search for `set wrap mode` did not reliably return that implementation. Rewording the declaration comment moved its result between third and fourth place, and copying the comment above the definition did not improve the result. Phase 4 moved complete concerns into named files; the shared shell that remains in `Editor.cxx` is intentional (paint, geometry, notifications, temporary dispatch).

The local grepai configuration uses 512-token chunks with 50-token overlap and the `nomic-embed-text` model. The upstream grepai chunker approximates this as fixed windows of about 2,048 characters with about 200 characters of overlap, breaks at newlines rather than C++ function boundaries, and adds the file path to the text embedded for every chunk. The required local build includes `.cxx` and `.iface` indexing plus the status wait interface introduced by `47bba43`; use `grepai version` and recorded benchmark metadata for the exact installed identity. Local benchmark results remain the authority, but the observed chunk boundaries agree with the [upstream chunker](https://github.com/yoanbernabeu/grepai/blob/main/indexer/chunker.go).

This means a short named method can be outweighed by unrelated neighboring code. It also means a descriptive file path and a file whose neighboring definitions cover one concern can improve every chunk without writing comments for a search engine.

Hybrid search improved exact-name lookup in the case study but did not solve intent lookup. With hybrid search temporarily enabled, `set wrap mode` ranked the documented declaration first, while `SetWrapMode` ranked the implementation seventh. Descriptive queries still preferred other wrapping code. grepai documents hybrid search as a combination of vector and text ranking intended for queries containing identifiers or keywords; it is a benchmark variable, not a substitute for clear source organization. See the [grepai hybrid-search guide](https://yoanbernabeu.github.io/grepai/hybrid-search/).

grepai call and reference analysis also has current limits. Tests reported declarations and definitions as callers, reported a method as its own callee, resolved `SelectionPosition::SetPosition` to `CallTip::SetPosition`, and returned no readers or writers for `wrap.state` or `xOffset`. Precise trace mode did not correct the overload error. These commands can suggest places to inspect, but they do not prove that a move or deletion is complete.

## Refactor rules

- Organize implementation files by a plain feature or concern name, such as `EditorWrapping.cxx`, `EditorScrolling.cxx`, or `EditorSelection.cxx`. Avoid catch-all names such as `Utilities`, `Settings`, or `Misc`.
- Keep neighboring code about the same concern. A natural-language search is useful when it reaches the right small concern file even if its first chunk contains a helper rather than the exact entry point.
- Move a complete concern in one reviewable sequence: named entry points, private helpers, focused tests, useful documentation, and temporary forwarding cases. Extracting one small wrapper while leaving its concern spread through `Editor.cxx` is not a discoverability result.
- Use the same plain feature nouns in filenames, operation names, state types, tests, and documentation where they describe the same thing. Prefer specific names such as `SetWrapMode`, `WrapPending`, and `WrapLines` over generic boundary names such as `Apply`, `Process`, or `SetState`.
- Keep the authoritative behavior description with the named operation beside its definition in the concern file. Do not maintain two detailed copies in the declaration and definition.
- Order each concern file for its own readable control flow rather than imposing one public-first or private-first layout. The autocomplete pilot benefited from named operations before a long private workflow; call tips worked best with the public operation directly beside its placement helper and with autocomplete cancellation owned by the public path.
- Write comments for readers. Explain choices, effects, units, delayed work, and exceptional behavior that the code does not make obvious. Do not repeat likely queries or add lists of synonyms to influence ranking.
- Remove obsolete API prose and deleted-feature documentation with the concern that replaces or deletes it. Transitional generated interfaces and forwarding cases may remain until their planned phase, but they must not be mistaken for the authoritative implementation.
- Keep direct control flow from a named entry point into its work. Delete numeric dispatch and avoid new indirection whose only purpose is routing between names.
- Do not arrange code around grepai's current chunk size or a particular embedding model. Concern-focused files and direct names must remain useful when tools and settings change.
- Treat search as a two-step workflow. Natural-language search should find the correct concern; exact or structural search should then find the named definition and its callers. Do not require vector search to replace `rg` when the name is already known.

## Search checks for each concern

Before moving a concern, record its authoritative implementation spans and the queries used to find it. After the move, wait for each changed file to reach the durable index, run the same queries, and compare ranks. Use the file's modification time as the lower bound:

```sh
grepai status --wait --steady --indexed scintilla/src/EditorWrapping.cxx --after "$(stat -c %Y scintilla/src/EditorWrapping.cxx)" --timeout 2m
```

The watcher incrementally reindexes only changed files. `--indexed` reloads the persisted store on each poll, so success proves that the file version at or after that modification time is available to a separate search process; `--steady` also proves that the watcher queue is idle. Indexed mtimes have one-second resolution: when the same file changes repeatedly, ensure the final edit has a different Unix-second mtime from the previously indexed version before waiting, or the older version can satisfy `--after`. Repeat the command for every changed file that the recorded search depends on. Use `grepai status --json` when a script needs the stable schema, durable generation, configuration hashes and match flags, pending counts, paths, or failure details.

- Exact name: `SetWrapMode`
- Spaced name: `set wrap mode`
- User intent: `turn wrapping on for long lines`
- Observable effect: `reset horizontal scrolling when wrapping changes`

An exact-name check must also use `rg`. It should find one authoritative definition, the required declaration, intentional call sites, and only the temporary forwarding paths allowed by the current phase.

Run semantic queries both across the whole repository and with `--path scintilla/src`. The whole-repository result measures the real default experience, including competition from `seed/`, generated interfaces, tests, and transitional documents. The path-limited result separates source organization from repository noise.

Run `grepai trace` and `grepai refs` only as recorded observations until their C++ results pass known-answer tests. Verify callers, state access, moved definitions, and deletion completion with `rg`, `ast-grep`, compilation, focused behavior tests, and the full check matrix.

## Benchmark corpus

Create the benchmark queries before the Phase 4 pilots and keep them unchanged while adjusting names, file boundaries, comments, or grepai settings. Include held-out paraphrases that were not consulted during the refactor.

The fixed corpus, runner, and result format live in [`tools/discoverability/`](tools/discoverability/README.md).

The current-tree baseline is recorded under [`benchmark-results/baseline/`](benchmark-results/baseline/observations.md) (phase 4 step 2). After that baseline the corpus is frozen: do not edit `queries.tsv` or `held-out-queries.tsv` while choosing pilot names, file boundaries, or comments. Change a corpus row only when this guide records the reason and the old and new evidence.

The initial corpus should cover at least these different shapes:

- Wrapping and view settings: `SetWrapMode`, `WrapCount`, and `SetScrollWidth`.
- Document state and notifications: `SetReadOnly` and `SetModEventMask`.
- Search with state changes: `SearchInTarget`.
- Keyboard commands: `Undo` and `LineDown`.
- Popup features owned by `ScintillaBase`: `AutoCShow` and `CallTipShow`.
- A retained feature not used by the first application: `FormatRange`.
- A platform-only deletion candidate: `SetTechnology`.

For each retained feature, include an exact identifier, a spaced identifier, a user-intent description, and an observable-effect description. For a deletion candidate, include queries that should stop finding a live implementation after deletion while still allowing the decision record to be found.

Record the expected concern file, entry-point definition, relevant test, and obsolete locations for every query. Store ranks rather than relying on raw similarity scores, since vector and hybrid scores are not directly comparable and can change with the indexed corpus.

## Benchmark matrix

Run the same corpus under these conditions:

- Baseline tree and refactored pilot.
- Vector-only search and hybrid search.
- Whole-repository search and `--path scintilla/src`.
- An index that includes the tree under test, with grepai version, embedding model, chunk size, overlap, boosts, ignore rules, durable generation and save time, configuration hash and match checks, and index hash recorded so a later run can tell whether tool, settings, or index changed.
- The normal query set and held-out paraphrases.

Do not require a full wipe-and-re-embed of the index for recorded runs. grepai already reindexes only files whose contents or modtimes changed; that is enough for fair rank comparison. A full rebuild is optional diagnostics if ranks look absurd or the index is known corrupt — not part of the pilot schedule. The phase 4 step 2 baseline did a full wipe once; nothing in that run showed the wipe was necessary, and later measurements should not repeat it by default.

Do not change more than one tool setting in a comparison. Refactor layout and tool configuration answer different questions and need separate results.

Add a boundary-stability check after each pilot. Add or remove about 200 characters before a selected entry point, use `grepai status --wait --steady --indexed ... --after ...` to wait for that exact file version in the durable index, and confirm that the correct concern remains easy to find. Restore the file and perform the same wait again before continuing. This checks that a result does not depend on an accidental fixed-window boundary.

Add cold navigation checks for the baseline and pilot. Ask a reader or agent to locate a feature, summarize its effects, identify its callers, and name its focused tests without first giving it the symbol name. Record the searches used, wrong files opened, and whether it reached the authoritative implementation.

## Initial acceptance criteria

- Every exact-name query finds the authoritative definition with `rg`.
- With the chosen normal grepai configuration, every exact-name query places the correct concern file in the top three results. The corresponding hybrid query places the authoritative definition in the top three; `rg` remains the authority for the exact definition and callers.
- At least 80 percent of natural-language and effect queries place the correct concern file in the top three results.
- A cold navigation check reaches the authoritative implementation in no more than two search steps.
- Held-out queries do not regress when comments or file boundaries change.
- Boundary-stability checks keep the correct concern in the top three.
- Obsolete documentation and reference code do not outrank the authoritative concern after the phase that removes them.
- The concern's focused behavior tests and `./check.sh` pass independently of the search benchmark.

The chosen normal configuration for these criteria is vector search over the whole repository (live `search.hybrid.enabled: false`). Path-limited cells and hybrid natural-language results are diagnostic comparisons; hybrid exact-name definition rank is the secondary exact-name gate.

The wrapping pilot adjusted the exact-name rule after `WrapCount` demonstrated a conflict in the initial criteria. Vector search ranked `EditorWrapping.cxx` first before and after a reader-oriented attempt to move the short definition earlier in the file, but it did not return the definition's chunk in the first ten; hybrid search ranked the definition first, and `rg` found its single definition directly. Requiring the vector engine to select a particular chunk would contradict the two-step search rule and encourage layout tied to the current fixed windows. Pilot acceptance counts the features moved by that pilot. The full corpus still runs and records changes to unmoved concerns so later steps can see broad effects without treating a temporary chunk shift in `Editor.cxx` as evidence about the moved concern.

The autocomplete and call-tip pilot approved placing authoritative descriptions beside definitions, but rejected a universal source-order rule. Moving all call-tip public operations before private work regressed the held-out query; keeping the public show operation beside its point-based helper, while making the public path directly own autocomplete cancellation, passed the normal, held-out, and boundary checks. Organize each concern around direct readable control flow, then verify it with the fixed corpus.

Baseline (pre-pilot) against that default cell: exact-name `rg` passes; exact-name definition top-3 is far below the bar; natural-language concern top-3 is about half of queries; HTML and seed often outrank live code; wrap cold nav reaches the right file in one step but the wrong span; undo cold nav fails. Full tables and the boundary re-run are in [`benchmark-results/baseline/observations.md`](benchmark-results/baseline/observations.md). Pilots are measured by improvement over that record, not by matching a path-limited hybrid cell that already looks healthier on the mixed tree.

If a pilot misses these criteria, inspect concern boundaries, competing stale material, whether the index still reflects the moved files, and search configuration before changing comment wording. Record any adjusted criterion and the evidence for it here rather than silently tuning the benchmark to the result.

## Phase 4 pilot sequence

The first pilot is the complete wrapping concern, not another isolated `SetWrapMode` edit. Move the wrapping entry points and private wrapping work into a clearly named concern file, move the useful documentation with them, retain only the temporary forwarding paths required by the phase, and run the benchmark before and after.

The second pilot must have a different shape. Autocomplete and call tips are the preferred candidate because they live in `ScintillaBase`, manage popup state, and are retained even though the first Wayland application will initially provide explicit unsupported host operations. A successful second pilot reduces the risk that the wrapping vocabulary or `Editor` layout alone produced the improvement.

Apply the full Phase 4 inventory only after both pilots improve the held-out benchmark without keyword-filled comments or layout choices tied to one chunk boundary.
