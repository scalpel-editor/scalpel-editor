# Discoverability lessons

This document records what the Phase 4 baseline and wrapping pilot taught us about organizing source code for readers and improving grepai. It separates measured results from interpretations and proposals. The benchmark procedure and current acceptance criteria remain in [DISCOVERABILITY.md](DISCOVERABILITY.md); detailed evidence is in the [baseline observations](benchmark-results/baseline/observations.md) and [wrapping-pilot observations](benchmark-results/wrapping-pilot/observations.md).

These findings come from one completed concern pilot. Wrapping is useful evidence because it combines short public operations, private layout work, deferred work, documentation, and observable editor effects. It is not enough evidence to approve every rule for the full refactor. The autocomplete and call-tip pilot must test the findings against popup state, a different owner, and features with no initial application consumer.

## Measured results

The baseline left wrapping inside the roughly 9,000-line `Editor.cxx`. Phase 2 had already given `SetWrapMode` a direct name and focused tests, but vector search often returned another part of the file instead of the operation. The wrapping pilot moved the complete concern into `EditorWrapping.cxx`.

| Check | Baseline | Wrapping pilot |
| --- | --- | --- |
| Normal wrapping queries with concern in top three, vector over repository | 8/8, with ranks 1 to 3 | 8/8, all rank 1 |
| `SetWrapMode` exact definition rank, vector over repository | absent from top 10 | rank 1 |
| `WrapCount` exact definition rank, vector over repository | absent from top 10 | absent from top 10; concern file rank 1 |
| `SetWrapMode` held-out concern rank | 3 | 2 |
| `WrapCount` held-out concern rank | 8 | 1 |
| Exact definitions, hybrid over repository | mixed baseline results | both rank 1 |
| Cold navigation | correct file, wrong span, then exact search | correct concern file, wrong span, then exact search |
| Default boundary check | wrapping concern stayed in top three | all ten wrapping ranks unchanged |

The full corpus also recorded a broad regression for concerns that remained in `Editor.cxx`: normal natural-language top-three results fell from 52.8% to 19.4%, and held-out top-three results fell from 25% to 16.7%. Their code had not moved. The most likely cause is that removing an early wrapping block shifted the fixed character windows for all later code in the file. This is evidence about the instability of the current chunker, not evidence that the moved wrapping concern became harder to find.

## Lessons for source organization

### Organize files by a coherent concern

A descriptive path improves every indexed chunk because grepai includes the path in the embedded text. More importantly, a concern-focused file makes neighboring definitions support the same reader question. `EditorWrapping.cxx` was a stronger search target than an equally named method surrounded by unrelated code in `Editor.cxx`.

File size alone is not the rule. A small catch-all file can still mix unrelated work, while a larger file can remain coherent. The useful boundary is the set of operations, private work, state, documentation, and tests that a reader needs to understand one feature area.

### Move the complete concern

Extracting only a public wrapper does not produce a clear destination when the implementation, helper work, documentation, and tests remain scattered. A concern move should include named entry points, private helpers, useful nearby documentation, focused tests, and temporary forwarding paths that still exist during the transition.

This also gives deletion checks a concrete shape: the old mixed file should retain only explicitly planned forwarding or shared work, not small wrappers that force readers back into it.

### Use one plain vocabulary

The same feature nouns should appear in filenames, operation names, state names, focused tests, and documentation. `EditorWrapping.cxx`, `SetWrapMode`, `WrapPending`, `WrapLines`, and `EditorWrappingTest.cxx` form a trail that works for exact search, descriptive search, and direct browsing.

Generic names such as `Apply`, `Process`, `Settings`, or `Utilities` weaken that trail. A name should say which editor behavior it controls rather than only what kind of code construct it is.

### Put the authoritative explanation beside the work

Old HTML descriptions often outranked live code in the baseline. Detailed documentation should move beside the retained operation, while obsolete descriptions should be removed with the behavior or interface they describe. Declarations should stay brief when the definition already carries the useful explanation.

Comments should explain effects, units, delayed work, exceptional behavior, and design choices. Repeating likely queries or adding synonym lists may change a benchmark score, but it does not make the source more trustworthy for a reader.

### Make focused tests part of the concern

A reader locating a feature also needs a quick way to learn what behavior is protected. A concern-named test file and test cases phrased in visible effects make that path direct. For wrapping, the tests expose horizontal-scroll reset, invalidation, scrollbar changes, repeated settings, visual options, display-row counts, resize, and modification handling.

Tests are also the guard against a source move that looks mechanical but accidentally changes callbacks or delayed work. Search results alone cannot establish that behavior was preserved.

### Design for two search steps

Descriptive search and exact search answer different questions. The first should locate the correct concern. Once a reader learns an operation name, `rg`, structural search, or a reliable hybrid search should locate the definition and callers.

The wrapping pilot reached `EditorWrapping.cxx` first for the prompt "turn wrapping on for long lines", but the returned span was internal layout work. An exact second search reached the public operations and tests. That is an acceptable path when the concern file is coherent; requiring vector search to select every short definition encourages source changes tied to one model and chunk layout.

### Do not preserve accidental chunk boundaries

Moving `WrapCount` earlier in the file did not make its exact vector query return the definition chunk, so the experiment was reverted. Padding `Editor.cxx` to keep later fixed windows unchanged would preserve tool accidents at the cost of source clarity.

Reader-oriented boundaries must remain useful when the embedding model, chunk size, or search engine changes. Boundary tests are valuable as diagnostics, but code should not be arranged around a specific window length.

### Measure the whole repository

Source-limited search measures the organization of live code. Whole-repository search measures the actual default experience, including seed references, generated interfaces, tests, HTML, and transitional documents. Both are useful, but only the whole-repository result reveals when old material outranks the implementation.

The full fixed corpus should continue to run for every pilot. Acceptance can focus on the concern being moved, while changes to unmoved concerns remain recorded as evidence about repository noise and chunk stability.

## Lessons for grepai

The following are improvement opportunities, not requirements for this repository. They are ordered roughly by the size of the observed problem and the likelihood that a tool change would help many codebases.

### 1. Chunk on language structure

The current fixed windows can combine unrelated functions, split a short definition from its documentation, and redivide all later code after an early edit. C++ definitions should be primary chunk boundaries.

A practical layered strategy would be:

- For a short or medium definition, index its leading documentation, qualified signature, and body as one chunk.
- For a large definition, index a definition-level record plus smaller chunks formed from statement blocks or syntax-tree nodes. Prefix each smaller chunk with the qualified function name and file path.
- For a class or concern file, index a lightweight file-level record containing the path and symbol inventory so descriptive search can select the concern before ranking spans within it.
- Give chunks stable identities based on qualified symbols and structural paths so editing one definition does not redivide unrelated later definitions.
- Use overlap at structural boundaries, such as carrying the enclosing signature into a body chunk, instead of relying only on a fixed number of neighboring characters.

This strategy must handle generated code, macros, incomplete parses, and very large functions without silently dropping content. Falling back to fixed windows for unparsed regions is preferable to failing indexing, but the fallback should be visible in status or diagnostics.

### 2. Route identifier queries to literal ranking

Vector search placed `EditorWrapping.cxx` first for `WrapCount` but did not return the definition chunk. Hybrid search placed the definition first. grepai could detect identifier-shaped queries and automatically give more weight to literal matches without requiring the user to change global configuration.

Identifier handling should preserve the literal token and also split common forms: `WrapCount`, `wrap count`, and `wrap_count` should reinforce each other. Qualified names should receive stronger definition matches than incidental mentions.

### 3. Distinguish definitions from other mentions

The index should retain whether a match is a definition, declaration, call, generated interface entry, documentation mention, or reference copy. Exact-name ranking should normally prefer a live definition, while descriptive ranking may still prefer a useful concern-level record.

This requires reliable language parsing and qualified-name handling. The current C++ reference analysis confused overloads, sometimes identified declarations or definitions as callers, and resolved one method to an unrelated method with the same short name. Until those cases pass known-answer tests, `rg`, structural search, compilation, and focused tests remain the authority.

### 4. Return concern and definition results together

When the best descriptive match is internal work in the correct file, grepai could show both the best concern match and the best definition within that concern. Grouping results by symbol or file would also prevent many nearby chunks from crowding the result list.

A useful result could say that `EditorWrapping.cxx` is the best concern, show its highest-ranked span, and separately list `Editor::SetWrapMode` as the strongest public definition. This preserves the value of the internal match without making the reader perform another broad search.

### 5. Model repository roles

The baseline showed live source competing with generated interfaces, old HTML, seed code, and tests. Simple `/src/` boosts cannot distinguish `scintilla/src` from `seed/.../src`. grepai could support explicit repository roles such as live source, tests, generated material, documentation, and historical reference.

Roles should adjust ranking rather than require exclusion. A reader may still need the seed implementation or old interface, but those should not silently outrank the current definition for a live feature.

### 6. Make index reads stable during watcher updates

The first wrapping-pilot matrix failed with `unexpected EOF` because the watcher rewrote `index.gob` while the runner was reading it. The benchmark runner now takes a hash-verified copy, but grepai itself should give readers a stable index.

Possible designs include writing a complete temporary index and replacing the old file atomically, keeping immutable index generations while readers hold them, or using a read protocol that cannot observe a partial write. A normal search should not need an external snapshot step.

### 7. Report watcher state without assuming PID visibility

`grepai status` reads a PID file and checks the process with signal 0. A command in a separate PID namespace cannot see the host watcher, so the check reports "not running" even while indexing continues. The result also looks like a stale PID, which can trigger an attempted cleanup.

A watcher heartbeat, held lock, local control socket, or an explicit "unknown from this process namespace" state would distinguish an invisible watcher from a stopped one. At minimum, failure to observe a PID should not be presented as definite proof that the watcher stopped when the runtime environment can hide processes.

### 8. Separate search settings from runtime metadata

The recorded config hash includes `watch.last_index_time`, so the hash changes even when chunking and ranking settings do not. grepai should expose a stable settings hash that excludes watcher timestamps and other runtime fields while retaining the full config for diagnosis.

## Evaluation lessons

A single query is too sensitive to wording and boundaries to guide a refactor. The fixed benchmark needs exact names, spaced names, user intent, visible effects, and held-out paraphrases. The held-out set is especially important because comments or names chosen after reading every query can overfit the measured corpus.

Vector and hybrid modes should be recorded separately because their scores are not comparable and they serve different query shapes. Repository-wide and source-limited scopes should also remain separate because they answer different questions.

Boundary checks reveal rank dependence on fixed windows, but they should not become a demand that every diagnostic cell remain identical. Cold navigation adds a reader-level check: it records searches, wrong files opened, the route to callers and tests, and whether the authoritative implementation is reached quickly.

Search measurement does not replace behavior verification. Every concern move still needs focused editor tests, exact completion searches, compilation, and the full sanitizer matrix.

## Current repository decisions

- Continue organizing `Editor` and `ScintillaBase` implementation files by coherent editor concerns.
- Move complete concerns rather than isolated wrappers.
- Keep one plain vocabulary across implementation, tests, and nearby documentation.
- Use vector search to locate a concern, hybrid or exact search to locate a named definition, and `rg` or structural search to verify definitions and callers.
- Keep the fixed corpus and held-out queries unchanged during the Phase 4 pilots.
- Record broad regressions without padding source to preserve fixed windows.
- Keep benchmark reads on a verified index snapshot while grepai can expose a partial watcher write.
- Treat sandboxed watcher status as unreliable and confirm completed indexing from the watcher log.

## Questions for the second pilot

The autocomplete and call-tip pilot should answer the following before these lessons become the rule for the broad split:

- Does a concern-focused file outrank helper types such as `AutoComplete` when the user asks to show or complete a popup?
- Should autocomplete and call tips share one popup concern or live in separate concern files?
- Can typed public operations, private popup state, notifications, cancellation, and completion remain easy to follow without recreating dispatch through another layer?
- Do authoritative comments beside definitions improve navigation without making declarations or helper types harder to find?
- Does structure-aware source organization help features that remain compiled but have no initial application consumer?
- Do held-out and boundary results improve for this different concern shape?
- Does cold navigation reach the operation, popup state, host effects, and focused tests within two search steps?
