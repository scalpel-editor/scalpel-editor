def top3($field): map(select(.[$field] != null and .[$field] <= 3)) | length;
def found($field): map(select(.[$field] != null)) | length;
def percent($count; $total): if $total == 0 then 0 else (($count * 1000 / $total) | round) / 10 end;

sort_by(.mode, .scope, .set)
| group_by(.mode, .scope, .set)
| map(
	. as $cell
	| (map(select(.disposition != "delete"))) as $retained
	| (map(select(.disposition == "delete"))) as $deleted
	| ($retained | map(select(.kind == "exact"))) as $exact
	| ($retained | map(select(.kind != "exact"))) as $natural
	| ($retained | top3("concern_rank")) as $concern_top3
	| ($natural | top3("concern_rank")) as $natural_top3
	| {
		mode: .[0].mode,
		scope: .[0].scope,
		set: .[0].set,
		n: ($retained | length),
		concern_top3: $concern_top3,
		concern_top3_pct: percent($concern_top3; ($retained | length)),
		concern_found: ($retained | found("concern_rank")),
		definition_top3: ($retained | top3("definition_rank")),
		definition_found: ($retained | found("definition_rank")),
		exact: {
			n: ($exact | length),
			definition_top3: ($exact | top3("definition_rank")),
			concern_top3: ($exact | top3("concern_rank"))
		},
		natural_language: {
			n: ($natural | length),
			concern_top3: $natural_top3,
			concern_top3_pct: percent($natural_top3; ($natural | length))
		},
		misses: [$retained[] | select(.concern_rank == null) | .id],
		deleted: {
			n: ($deleted | length),
			decision_record_found: ($deleted | found("concern_rank"))
		}
	}
)
