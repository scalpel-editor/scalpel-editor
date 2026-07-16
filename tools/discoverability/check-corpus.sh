#!/usr/bin/env bash
# Check that the Phase 4 discoverability corpus describes files in the final tree.

set -euo pipefail

fail() {
	printf 'discoverability corpus: %s\n' "$*" >&2
	exit 1
}

root=$(git rev-parse --show-toplevel)
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
corpora=("$script_dir/queries.tsv" "$script_dir/held-out-queries.tsv")

for corpus in "${corpora[@]}"; do
	awk -F '\t' '
		NR == 1 {
			if (NF != 11 || $1 != "id" || $11 != "disposition")
				exit 1
			next
		}
		NF != 11 { exit 1 }
		{ for (field = 1; field <= 11; field++) if ($field == "") exit 1 }
	' "$corpus" || fail "invalid row in ${corpus#$root/}"
done

duplicate_ids=$(
	{ tail -n +2 "${corpora[0]}"; tail -n +2 "${corpora[1]}"; } |
		cut -f1 | sort | uniq -d
)
[[ -z $duplicate_ids ]] || fail "duplicate query ids: $duplicate_ids"

row_count=0
while IFS=$'\t' read -r id feature kind query baseline_file target_file baseline_definition target_definition test obsolete_locations disposition; do
	[[ $id == id ]] && continue
	case $kind in
		exact|spaced|intent|effect|held-out) ;;
		*) fail "$id has unknown query kind: $kind" ;;
	esac
	case $disposition in
		retain|command|delete) ;;
		*) fail "$id has unknown disposition: $disposition" ;;
	esac
	[[ -f $root/$target_file ]] || fail "$id target does not exist: $target_file"
	[[ -f $root/$test ]] || fail "$id evidence does not exist: $test"
	rg -F -q -- "$target_definition" "$root/$target_file" ||
		fail "$id definition marker is absent from $target_file: $target_definition"
	row_count=$((row_count + 1))
done < <({ cat "${corpora[0]}"; tail -n +2 "${corpora[1]}"; })

printf 'discoverability corpus: %d rows have valid final targets and evidence\n' "$row_count"
