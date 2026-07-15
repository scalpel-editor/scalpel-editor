# Discoverability benchmark

This directory contains the fixed Phase 4 search corpus and the runner that records grepai ranks. `queries.tsv` contains the normal exact-name, spaced-name, intent, and effect queries. `held-out-queries.tsv` contains paraphrases that are evaluated only after names, comments, and file boundaries have been chosen; do not use them to guide those choices.

Each corpus row records the feature, query kind, baseline and target concern files, text that identifies the authoritative definition, expected focused test, obsolete locations, and whether the feature is retained, converted to a command, or deleted. Target file names are part of the fixed benchmark. Change a corpus row after the baseline only when `DISCOVERABILITY.md` records the reason and the old and new evidence.

The installed grepai build must support `.cxx`, `.hxx`, and `.iface` indexing plus the status wait interface added by `47bba43`. Check the current build with `grepai version`; do not rely on a version written into this guide. The runner records the installed binary hash, source revision, source diff hash, live config hash, index hash, and `grepai status` output so later runs can tell whether the tool or index changed.

## Run

Before a recorded run, make sure the durable index includes every changed file. Leave the watcher running while you edit, then wait for each required file version with its project-relative path and modification time:

```sh
file=scintilla/src/EditorWrapping.cxx
grepai status --wait --steady --indexed "$file" --after "$(stat -c %Y "$file")" --timeout 2m
```

Repeat the wait for each changed file that affects the run. Do not replace it with a sleep or log search: `--indexed` reloads the persisted store on every poll and returns only after the requested file version is durable, while `--steady` also requires an idle queue. Indexed mtimes have one-second resolution, so a second edit in the same Unix second as the previously indexed version needs a later final mtime before `--after` can distinguish it. The plain report includes durable generation and save time, configuration identities and match checks, paths, pending work, and the last failure; use `--json` for the stable machine-readable schema. Do not delete `index.gob` and re-embed the whole project unless the index is broken or you are deliberately testing rebuild behavior. Stop editing during the matrix; the runner takes a hash-verified snapshot and records its hash so vector and hybrid cells use one fixed index.

Run the complete matrix (normal and held-out, both modes and scopes) from the repository root:

```sh
tools/discoverability/run-searches.sh --label baseline --output benchmark-results/baseline --set all
```

Run one query while checking the runner:

```sh
tools/discoverability/run-searches.sh --label smoke --output /tmp/scalpel-discoverability-smoke --mode vector --scope source --query-id wrap-set-mode-exact
```

After the wrapping pilot, use target expectations only for the moved features:

```sh
tools/discoverability/run-searches.sh --label wrapping-pilot --output benchmark-results/wrapping-pilot --set all --target-feature SetWrapMode --target-feature WrapCount
```

Use `--set held-out` for the held-out pass alone and `--all-target` only after every benchmark feature has reached its final Phase 4 location. The runner searches the existing index.

Use `--feature` to limit a diagnostic run without changing the fixed corpus. The wrapping boundary check uses both moved features:

```sh
tools/discoverability/run-searches.sh --label wrapping-pilot-boundary --output benchmark-results/wrapping-pilot/boundary --set all --feature SetWrapMode --feature WrapCount --target-feature SetWrapMode --target-feature WrapCount
```

grepai has no command-line hybrid switch in this build. The runner takes a hash-verified snapshot of the live index, then creates temporary project roots containing a mode-adjusted copy of `.grepai/config.yaml` and a symlink to that snapshot. It does not modify the live config or index. Vector and hybrid runs therefore use the same indexed chunks and differ only in `search.hybrid.enabled`.

## Checked-in baseline

Phase 4 step 2 recorded the pre-pilot tree in [`benchmark-results/baseline/`](../../benchmark-results/baseline/). That directory holds the frozen-index search matrix, `summary.json` roll-ups, and `observations.md` (exact-name `rg`, boundary-stability, cold navigation, and acceptance snapshot). The corpus is fixed after that baseline; do not edit the TSV files while piloting.

The completed pilot records are [`benchmark-results/wrapping-pilot/`](../../benchmark-results/wrapping-pilot/observations.md) and [`benchmark-results/autocomplete-calltip-pilot/`](../../benchmark-results/autocomplete-calltip-pilot/observations.md). Each contains the full matrix, boundary checks, exact-source observations, cold navigation, and its acceptance decision.

## Results

Each output directory contains:

- `metadata.json`: repository state, grepai identity, config and index hashes, and the selected matrix cells.
- `grepai-config.yaml`: the run configuration with any API key redacted.
- `grepai-status.txt`: grepai's index status at the start of the run, including watcher state, pending work, durable generation and save time, configuration match checks, paths, and the last failure.
- `search-results.jsonl`: one JSON object for each query, mode, and scope.
- `completion.json`: completion time and number of searches written.

Each JSONL object stores the ranked paths, line spans, and scores plus `concern_rank`, the first result from the expected concern file, and `definition_rank`, the first result from that file whose chunk contains the recorded definition text. Chunk content is used to calculate the definition rank but is not retained in the result file. `obsolete_hits` records ranked results from locations that should disappear as the concern is migrated. Ranks are one-based and `null` means the expected result was absent from the first 10 results.

The automatic ranks do not replace source checks. Use `rg` to verify exact definitions and callers, and record boundary-stability and cold-navigation observations separately because they require a temporary source change or a reader's navigation record.
