#!/usr/bin/env bash

set -euo pipefail

usage() {
	cat <<'EOF'
Usage: run-searches.sh --label LABEL --output DIRECTORY [options]

Options:
  --mode vector|hybrid|both       Search mode to run (default: both)
  --scope repository|source|both Search scope to run (default: both)
  --set normal|held-out|all       Query set to run (default: normal)
  --query-id ID                   Run only one query
  --target-feature NAME           Use target expectations for one feature; repeatable
  --all-target                    Use target expectations for every feature
  -h, --help                      Show this help
EOF
}

fail() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

label=
output=
mode=both
scope=both
query_set=normal
query_id=
all_target=false
target_features=()

while (($#)); do
	case "$1" in
	--label)
		(($# >= 2)) || fail "--label needs a value"
		label=$2
		shift 2
		;;
	--output)
		(($# >= 2)) || fail "--output needs a value"
		output=$2
		shift 2
		;;
	--mode)
		(($# >= 2)) || fail "--mode needs a value"
		mode=$2
		shift 2
		;;
	--scope)
		(($# >= 2)) || fail "--scope needs a value"
		scope=$2
		shift 2
		;;
	--set)
		(($# >= 2)) || fail "--set needs a value"
		query_set=$2
		shift 2
		;;
	--query-id)
		(($# >= 2)) || fail "--query-id needs a value"
		query_id=$2
		shift 2
		;;
	--target-feature)
		(($# >= 2)) || fail "--target-feature needs a value"
		target_features+=("$2")
		shift 2
		;;
	--all-target)
		all_target=true
		shift
		;;
	-h|--help)
		usage
		exit 0
		;;
	*)
		fail "unknown argument: $1"
		;;
	esac
done

[[ -n $label ]] || fail "--label is required"
[[ -n $output ]] || fail "--output is required"
[[ $mode == vector || $mode == hybrid || $mode == both ]] || fail "invalid --mode: $mode"
[[ $scope == repository || $scope == source || $scope == both ]] || fail "invalid --scope: $scope"
[[ $query_set == normal || $query_set == held-out || $query_set == all ]] || fail "invalid --set: $query_set"

command -v grepai >/dev/null || fail "grepai is not available"
command -v jq >/dev/null || fail "jq is not available"
command -v sha256sum >/dev/null || fail "sha256sum is not available"

project_root=$(git rev-parse --show-toplevel)
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
config_file=$project_root/.grepai/config.yaml
index_file=$project_root/.grepai/index.gob

[[ -f $config_file ]] || fail "missing grepai config: $config_file"
[[ -f $index_file ]] || fail "missing grepai index: $index_file"
[[ ! -e $output ]] || fail "output already exists: $output"

validate_corpus() {
	local corpus=$1
	awk -F '\t' '
		NR == 1 {
			if (NF != 11 || $1 != "id" || $11 != "disposition")
				exit 1
			next
		}
		NF != 11 || $1 == "" || $2 == "" || $3 == "" || $4 == "" { exit 1 }
	' "$corpus" || fail "invalid corpus row in $corpus"
}

validate_corpus "$script_dir/queries.tsv"
validate_corpus "$script_dir/held-out-queries.tsv"

duplicate_ids=$(
	{ tail -n +2 "$script_dir/queries.tsv"; tail -n +2 "$script_dir/held-out-queries.tsv"; } |
		cut -f1 | sort | uniq -d
)
[[ -z $duplicate_ids ]] || fail "duplicate query ids: $duplicate_ids"

mkdir -p "$output"
results_file=$output/search-results.jsonl
: >"$results_file"

sed 's/^\([[:space:]]*api_key:\).*/\1 REDACTED/' "$config_file" >"$output/grepai-config.yaml"
(cd "$project_root" && grepai status) >"$output/grepai-status.txt"

grepai_binary=$(command -v grepai)
grepai_source=/my/src/grepai
grepai_source_revision=
grepai_source_branch=
grepai_source_dirty=false
grepai_source_diff_sha256=
if git -C "$grepai_source" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	grepai_source_revision=$(git -C "$grepai_source" rev-parse HEAD)
	grepai_source_branch=$(git -C "$grepai_source" branch --show-current)
	if [[ -n $(git -C "$grepai_source" status --short) ]]; then
		grepai_source_dirty=true
		grepai_source_diff_sha256=$(git -C "$grepai_source" diff --binary | sha256sum | cut -d' ' -f1)
	fi
fi

target_features_json=$(printf '%s\n' "${target_features[@]}" | jq -Rsc 'split("\n") | map(select(length > 0))')
project_dirty=false
[[ -z $(git -C "$project_root" status --short) ]] || project_dirty=true

jq -n \
	--arg schema_version "1" \
	--arg label "$label" \
	--arg started_at "$(date --utc +%Y-%m-%dT%H:%M:%SZ)" \
	--arg project_revision "$(git -C "$project_root" rev-parse HEAD)" \
	--argjson project_dirty "$project_dirty" \
	--arg grepai_version "$(grepai version)" \
	--arg grepai_binary "$grepai_binary" \
	--arg grepai_binary_sha256 "$(sha256sum "$grepai_binary" | cut -d' ' -f1)" \
	--arg grepai_source "$grepai_source" \
	--arg grepai_source_revision "$grepai_source_revision" \
	--arg grepai_source_branch "$grepai_source_branch" \
	--argjson grepai_source_dirty "$grepai_source_dirty" \
	--arg grepai_source_diff_sha256 "$grepai_source_diff_sha256" \
	--arg config_sha256 "$(sha256sum "$config_file" | cut -d' ' -f1)" \
	--arg index_sha256 "$(sha256sum "$index_file" | cut -d' ' -f1)" \
	--arg mode "$mode" \
	--arg scope "$scope" \
	--arg query_set "$query_set" \
	--arg query_id "$query_id" \
	--argjson all_target "$all_target" \
	--argjson target_features "$target_features_json" \
	'{
		schema_version: ($schema_version | tonumber),
		label: $label,
		started_at: $started_at,
		project: {revision: $project_revision, dirty: $project_dirty},
		grepai: {
			version: $grepai_version,
			binary: $grepai_binary,
			binary_sha256: $grepai_binary_sha256,
			source: $grepai_source,
			source_revision: $grepai_source_revision,
			source_branch: $grepai_source_branch,
			source_dirty: $grepai_source_dirty,
			source_diff_sha256: $grepai_source_diff_sha256
		},
		grepai_config_sha256: $config_sha256,
		grepai_index_sha256: $index_sha256,
		selection: {
			mode: $mode,
			scope: $scope,
			query_set: $query_set,
			query_id: (if $query_id == "" then null else $query_id end),
			all_target: $all_target,
			target_features: $target_features
		}
	}' >"$output/metadata.json"

temporary_root=$(mktemp -d)
trap 'rm -rf "$temporary_root"' EXIT

prepare_mode_root() {
	local search_mode=$1
	local enabled=false
	[[ $search_mode == hybrid ]] && enabled=true
	local mode_root=$temporary_root/$search_mode
	mkdir -p "$mode_root/.grepai"
	sed "/^[[:space:]]*hybrid:/,/^[[:space:]]*k:/ s/^\([[:space:]]*enabled:\).*/\1 $enabled/" \
		"$config_file" >"$mode_root/.grepai/config.yaml"
	ln -s "$index_file" "$mode_root/.grepai/index.gob"
	printf '%s\n' "$mode_root"
}

if [[ $mode == both ]]; then
	modes=(vector hybrid)
else
	modes=("$mode")
fi

if [[ $scope == both ]]; then
	scopes=(repository source)
else
	scopes=("$scope")
fi

if [[ $query_set == all ]]; then
	corpora=("$script_dir/queries.tsv" "$script_dir/held-out-queries.tsv")
elif [[ $query_set == held-out ]]; then
	corpora=("$script_dir/held-out-queries.tsv")
else
	corpora=("$script_dir/queries.tsv")
fi

uses_target_expectation() {
	local feature=$1
	[[ $all_target == true ]] && return 0
	local target_feature
	for target_feature in "${target_features[@]}"; do
		[[ $target_feature == "$feature" ]] && return 0
	done
	return 1
}

queries_run=0
for search_mode in "${modes[@]}"; do
	mode_root=$(prepare_mode_root "$search_mode")
	for search_scope in "${scopes[@]}"; do
		for corpus in "${corpora[@]}"; do
			set_name=normal
			[[ $corpus == *held-out-queries.tsv ]] && set_name=held-out
			while IFS=$'\t' read -r id feature kind query baseline_file target_file baseline_definition target_definition test obsolete_locations disposition; do
				[[ $id == id ]] && continue
				[[ -z $query_id || $query_id == "$id" ]] || continue

				expectation=baseline
				expected_file=$baseline_file
				expected_definition=$baseline_definition
				if uses_target_expectation "$feature"; then
					expectation=target
					expected_file=$target_file
					expected_definition=$target_definition
				fi

				search_args=(search "$query" --json --limit 10)
				[[ $search_scope == source ]] && search_args+=(--path scintilla/src)
				printf 'running %s %s %s\n' "$search_mode" "$search_scope" "$id" >&2
				search_results=$(cd "$mode_root" && grepai "${search_args[@]}")
				if jq -e 'type == "object" and has("error")' >/dev/null <<<"$search_results"; then
					fail "grepai search failed for $id: $(jq -r '.error' <<<"$search_results")"
				fi
				jq -e 'type == "array"' >/dev/null <<<"$search_results" || fail "grepai returned invalid JSON for $id"

				jq -c -n \
					--arg id "$id" \
					--arg set "$set_name" \
					--arg feature "$feature" \
					--arg kind "$kind" \
					--arg query "$query" \
					--arg mode "$search_mode" \
					--arg scope "$search_scope" \
					--arg expectation "$expectation" \
					--arg expected_file "$expected_file" \
					--arg expected_definition "$expected_definition" \
					--arg test "$test" \
					--arg obsolete_locations "$obsolete_locations" \
					--arg disposition "$disposition" \
					--argjson results "$search_results" \
					'($obsolete_locations | split(";")) as $obsolete |
					{
						schema_version: 1,
						id: $id,
						set: $set,
						feature: $feature,
						kind: $kind,
						query: $query,
						mode: $mode,
						scope: $scope,
						expectation: $expectation,
						expected_file: $expected_file,
						expected_definition: $expected_definition,
						expected_test: $test,
						disposition: $disposition,
						concern_rank: ([range(0; $results | length) as $i | select($results[$i].file_path == $expected_file) | $i + 1][0] // null),
						definition_rank: ([range(0; $results | length) as $i | select($results[$i].file_path == $expected_file and (($results[$i].content // "") | contains($expected_definition))) | $i + 1][0] // null),
						obsolete_hits: [range(0; $results | length) as $i | $results[$i].file_path as $path | select($obsolete | index($path)) | {rank: ($i + 1), file_path: $path}],
						results: [$results[] | {file_path, start_line, end_line, score}]
					}' >>"$results_file"
				queries_run=$((queries_run + 1))
			done <"$corpus"
		done
	done
done

((queries_run > 0)) || fail "no query matched the selection"
jq -n --arg completed_at "$(date --utc +%Y-%m-%dT%H:%M:%SZ)" --argjson queries_run "$queries_run" \
	'{completed_at: $completed_at, searches_run: $queries_run}' >"$output/completion.json"
printf 'wrote %d search results to %s\n' "$queries_run" "$output"
