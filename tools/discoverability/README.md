# Discoverability benchmark

This directory contains the fixed Phase 4 search corpus and the runner that records grepai ranks. `queries.tsv` contains the normal exact-name, spaced-name, intent, and effect queries. `held-out-queries.tsv` contains paraphrases that are evaluated only after names, comments, and file boundaries have been chosen; do not use them to guide those choices.

Each corpus row records the feature, query kind, baseline and target concern files, text that identifies the authoritative definition, expected focused test, obsolete locations, and whether the feature is retained, converted to a command, or deleted. Target file names are part of the fixed benchmark. Change a corpus row after the baseline only when `DISCOVERABILITY.md` records the reason and the old and new evidence.

The installed grepai build reports `dev-iface`. Its source is `/my/src/grepai` on branch `iface`, based on main plus commit `f1149ad` for `.cxx` and `.hxx` support, with the `.iface` scanner addition present in the working tree. The runner records the installed binary hash, source revision, source diff hash, live config hash, index hash, and `grepai status` output so later runs can tell whether the tool or index changed.

## Run

Run the complete normal matrix from the repository root:

```sh
tools/discoverability/run-searches.sh --label baseline --output benchmark-results/baseline
```

Run one query while checking the runner:

```sh
tools/discoverability/run-searches.sh --label smoke --output /tmp/scalpel-discoverability-smoke --mode vector --scope source --query-id wrap-set-mode-exact
```

After the wrapping pilot, use target expectations only for the moved features:

```sh
tools/discoverability/run-searches.sh --label wrapping-pilot --output benchmark-results/wrapping-pilot --target-feature SetWrapMode --target-feature WrapCount
```

Use `--set held-out` for the held-out pass and `--all-target` only after every benchmark feature has reached its final Phase 4 location. The runner searches the existing index; create and verify the fresh index required by `DISCOVERABILITY.md` before starting a recorded run.

grepai has no command-line hybrid switch in this build. The runner creates temporary project roots containing a mode-adjusted copy of `.grepai/config.yaml` and a symlink to the live read-only index. It does not modify the live config or index. Vector and hybrid runs therefore use the same indexed chunks and differ only in `search.hybrid.enabled`.

## Results

Each output directory contains:

- `metadata.json`: repository state, grepai identity, config and index hashes, and the selected matrix cells.
- `grepai-config.yaml`: the run configuration with any API key redacted.
- `grepai-status.txt`: grepai's index status at the start of the run.
- `search-results.jsonl`: one JSON object for each query, mode, and scope.
- `completion.json`: completion time and number of searches written.

Each JSONL object stores the ranked paths, line spans, and scores plus `concern_rank`, the first result from the expected concern file, and `definition_rank`, the first result from that file whose chunk contains the recorded definition text. Chunk content is used to calculate the definition rank but is not retained in the result file. `obsolete_hits` records ranked results from locations that should disappear as the concern is migrated. Ranks are one-based and `null` means the expected result was absent from the first 10 results.

The automatic ranks do not replace source checks. Use `rg` to verify exact definitions and callers, and record boundary-stability and cold-navigation observations separately because they require a temporary source change or a reader's navigation record.
