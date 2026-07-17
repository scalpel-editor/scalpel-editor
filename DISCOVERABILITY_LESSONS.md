# Discoverability lessons

This document records reusable lessons about organizing source code, evaluating navigation, and improving grepai. They come from two concern pilots followed by full refactor and deletion gates. The [scalpel-editor case study](DISCOVERABILITY_CASE_STUDY.md) records the project-specific setup, measurements, corrections, and decisions; [DISCOVERABILITY.md](DISCOVERABILITY.md) defines that project's benchmark procedure and acceptance criteria.

## Organizing source for readers

### Organize files by a coherent concern

grepai includes the path in the text it embeds, so a descriptive path adds a feature name to every indexed chunk. A concern-focused file also makes neighboring definitions support the same reader question. These properties can help the tool choose a candidate file; they do not make it reliably select the relevant definition or line span within that file.

File size alone is not the rule. A small catch-all file can still mix unrelated work, while a larger file can remain coherent. The useful boundary is the set of operations, private work, state, documentation, and tests that a reader needs to understand one feature area.

### Move the complete concern

Extracting only a public wrapper does not produce a clear destination when the implementation, helper work, documentation, and tests remain scattered. A concern move should include named entry points, private helpers, useful nearby documentation, focused tests, and any temporary forwarding paths that still exist during the transition.

This also gives completion checks a concrete shape: the old mixed file should retain only explicitly planned forwarding or shared work, not small wrappers that force readers back into it.

### Use one plain vocabulary

The same feature nouns should appear in filenames, operation names, state names, focused tests, and documentation. This creates a trail that works for exact search, descriptive search, and direct browsing.

Generic names such as `Apply`, `Process`, `Settings`, or `Utilities` weaken that trail. A name should say which behavior it controls rather than only what kind of code construct it is.

### Put the authoritative explanation beside the work

Detailed documentation should live beside the retained operation, while obsolete descriptions should be removed with the behavior or interface they describe. Declarations should stay brief when the definition already carries the useful explanation.

Comments should explain effects, units, delayed work, exceptional behavior, and design choices. Repeating likely queries or adding synonym lists may change a benchmark score, but it does not make the source more trustworthy for a reader.

Live comments should describe the current design directly. Negative comments that repeat deleted type or interface names keep obsolete vocabulary searchable and complicate absence checks. Detailed historical names belong in designated history documents.

### Keep related work contiguous inside a concern

A well-named file can still make a feature hard to follow when the state or policy that configures an operation is separated from the operation that consumes it. Keep closely related state, policy, entry points, and helpers together when their control flow is read as one unit.

There is no universal public-first or private-first ordering rule. Choose the order that makes the concern's direct control flow clear, then check it with descriptive and held-out queries.

### Make focused tests part of the concern

A reader locating a feature also needs a quick way to learn what behavior is protected. A concern-named test file and test cases phrased in visible effects make that path direct.

Tests are also the guard against a source move that looks mechanical but accidentally changes callbacks or delayed work. Search results alone cannot establish that behavior was preserved.

## Designing navigation

### Design for two search steps

Descriptive search and exact search answer different questions. The demonstrated use of descriptive grepai search is to suggest files that may contain the concern. Once a reader learns an operation name, literal search or structural search should locate the definition and callers.

Treat the returned line span as a hint, not as the answer. grepai can rank the correct concern file while showing an unrelated chunk from that file, even when the query closely matches documentation beside the desired definition. Opening the candidate file and running an exact second search is required for dependable navigation.

Exact identifiers and descriptive queries therefore need separate gates. Use descriptive queries to evaluate concern discovery and literal tools to prove exact definitions, declarations, and callers. Record vector or hybrid exact-name ranks as diagnostics rather than making them a release gate when broad identifiers also name legitimate lower-level work.

### Measure repeatability before comparing ranks

A single ranked run does not show that a result is stable. Repeat the same query against one fixed index and configuration, record rank variation, and define how repeated results are summarized before using small rank changes as evidence.

Separate invocations using plain and compact JSON output have produced different rankings for the same query. Because each invocation embeds and searches again, that observation does not prove the output format caused the change. It does prove that a one-pass benchmark does not measure repeatability.

### Do not preserve accidental chunk boundaries

Moving a method or padding an earlier file region solely to improve one rank preserves a tool accident at the cost of source clarity. Reader-oriented boundaries must remain useful when the embedding model, chunk size, or search engine changes.

Boundary tests are valuable diagnostics because they expose rank dependence on fixed windows. They should not become a demand that every diagnostic result remain identical after a harmless nearby edit.

### Measure the whole repository

Source-limited search measures the organization of live code. Whole-repository search measures the actual default experience, including tests, generated material, old documentation, reference code, and transitional records. Both are useful, but only the whole-repository result reveals when non-authoritative material outranks the implementation.

Do not index benchmark output. Result files repeat queries, expected definitions, and target paths; indexing them makes earlier measurements compete with the source and causes the benchmark to change the thing being measured.

### Treat ranks as evidence, not a verdict

Removing repository noise can improve some queries, leave others unchanged, and move still others down because the indexed corpus, chunk boundaries, and repeated search results can change. A rank change alone does not establish whether a refactor improved navigation.

Interpret ranks with exact-source audits, cold navigation, concern coherence, held-out queries, repeated trials, and behavior checks. Record regressions instead of padding source or repeating keywords to erase them.

## Evaluation and completion

### Use a fixed, varied corpus

A single query is too sensitive to wording and boundaries to guide a refactor. A useful corpus includes exact names, spaced names, user intent, visible effects, and held-out paraphrases. The held-out set matters because names or comments chosen after reading every query can overfit the measured corpus.

Record vector and hybrid modes separately because they serve different query shapes. Keep whole-repository and source-limited scopes separate because they answer different questions.

### Validate benchmark expectations as data

An invalid expected path or test can turn a correct result into a false failure. Validate every corpus row before running an expensive matrix: require unique IDs, existing target files and evidence files, known query kinds and dispositions, and an expected definition marker present in the target.

Validate the final tree, not only the corpus format. A syntactically valid row can still refer to a file that was planned but never created.

### Set expectations per query

One feature can have different correct destinations. An exact command-name query may belong at command dispatch, while intent and effect queries belong at the operation that performs the work. Store the expected concern and definition per query rather than assigning one destination to every query for a feature.

Document expectation corrections with their old and new evidence. Do not change query text or targets merely to improve a score.

### Score retained and deleted features separately

A retained feature succeeds when readers reach working code. A deleted feature succeeds when no live implementation remains and its deletion decision is findable. Combining both in one concern-rank percentage makes the result misleading.

The same distinction applies to completion checks: retained paths need positive evidence, while deleted paths need absence checks plus a designated historical record.

### Run cheap audits before expensive evidence

Use this order for a final record: validate the corpus, run completion scans, audit exact names, perform cold navigation, correct real gaps, wait for the durable index, then record the full benchmark snapshot. This avoids spending time and provider work on a matrix that a quick source search would already invalidate.

Search measurement does not replace behavior verification. Concern moves still need focused tests, compilation, and the project's broader runtime or sanitizer checks.

### Make completion allowlists precise

Allow exact token and path pairs, with an owner and reason for each exception. Do not exclude an entire file because it contains one legitimate spelling; that can hide forbidden uses elsewhere in the same file.

Test absence gates in both directions. Representative legitimate external terms must pass, and representative forbidden terms must fail. File-absence checks alone are insufficient because deleted type names or live instructions can survive in other files.

## Improvements for grepai

The following are product improvement opportunities, not requirements for repositories using grepai.

### 1. Chunk on language structure

Fixed windows can combine unrelated functions, split a short definition from its documentation, and redivide all later code after an early edit. Definitions should be primary chunk boundaries for supported languages.

A practical layered strategy would be:

- For a short or medium definition, index its leading documentation, qualified signature, and body as one chunk.
- For a large definition, index a definition-level record plus smaller chunks formed from statement blocks or syntax-tree nodes, prefixing each smaller chunk with the qualified function name and file path.
- For a class or concern file, index a lightweight file-level record containing the path and symbol inventory so descriptive search can select the concern before ranking spans within it.
- Give chunks stable identities based on qualified symbols and structural paths so editing one definition does not redivide unrelated later definitions.
- Use overlap at structural boundaries, such as carrying the enclosing signature into a body chunk, instead of relying only on a fixed number of neighboring characters.

This strategy must handle generated code, macros, incomplete parses, and very large functions without silently dropping content. Falling back to fixed windows for unparsed regions is preferable to failing indexing, but status or diagnostics should identify the fallback.

### 2. Route identifier queries to literal ranking

grepai could detect identifier-shaped queries and automatically give more weight to literal matches without requiring users to change global configuration. Identifier handling should preserve the literal token and also split common forms such as `WrapCount`, `wrap count`, and `wrap_count` so they reinforce one another.

Qualified definition matches should rank above incidental mentions. This requires dependable parsing and qualified-name handling; until known-answer tests cover overloads and same-spelling methods, literal search, structural search, compilation, and focused tests remain the authority.

### 3. Return concern and definition results together

When the best descriptive match is internal work in the correct file, grepai could show both the best concern match and the best definition within that concern. Grouping results by symbol or file would also prevent many nearby chunks from crowding the result list.

### 4. Model repository roles

Explicit repository roles such as live source, tests, generated material, documentation, historical reference, and benchmark output would rank the default experience more accurately than a broad path boost.

Roles should usually adjust ranking rather than require exclusion. Benchmark output is the exception because it directly repeats the evaluation inputs and should not be a search candidate during its own measurement.

### 5. Make index reads stable during watcher updates

A reader should never observe a partially written index. Possible designs include writing a complete temporary index and replacing the old file atomically, keeping immutable generations while readers hold them, or using a read protocol that cannot observe a partial write.

### 6. Support batch durability barriers

Large refactors may change dozens of indexed files. The status command should accept repeated `--indexed` arguments or a manifest, confirm all requested versions against one durable generation, and report every missing or stale file together.

### 7. Make read-only watcher status complete (partly resolved)

grepai `47bba43` added watcher snapshots, solving the problem of a sandbox not sharing the watcher's process namespace. Read-only clients can still fail if waiting requires opening a lock file for writing. Waiting should consume the readable watcher snapshot and persisted index without write access to the lock, or treat lock inspection as optional.

### 8. Separate search settings from runtime metadata (resolved)

grepai `47bba43` replaced a configuration hash that included volatile watcher timestamps with a stable hash of index-shaping settings. Status now reports current, watcher, and saved-index identities; match checks; newest indexed source time; durable save time; and durable generation separately.

### 9. Make benchmark output transactional

A runner should verify provider access before creating its final output directory. It should write results into a temporary directory and rename it into place only after every search and summary succeeds, so a provider failure cannot leave a partial record that looks complete.
