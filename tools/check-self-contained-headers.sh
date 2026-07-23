#!/bin/sh
# Fail when listed production headers do not compile alone (IWYU prerequisite).
#
# Usage:
#   tools/check-self-contained-headers.sh --changed    # production headers dirty vs HEAD
#   tools/check-self-contained-headers.sh path/to/H.h  # one or more headers
#   tools/check-self-contained-headers.sh              # every app/ and Scintilla production header
#   tools/check-self-contained-headers.sh --all-src    # every scintilla/src/*.h only (legacy alias)
#
# Prefer --changed or explicit paths during development. The no-argument full scan is for occasional audits and phase gates, not every session.
#
# Requires a configured build tree with compile_commands.json (cmake --preset dev).

set -eu

script_dir=$(dirname -- "$0")
root=$(CDPATH= cd -- "$script_dir/.." && pwd)
cd "$root"

compile_db=
for candidate in build/compile_commands.json build-asan/compile_commands.json; do
	if [ -f "$candidate" ]; then
		compile_db=$candidate
		break
	fi
done
if [ -z "$compile_db" ]; then
	printf '%s\n' "error: no compile_commands.json under build/ (run: cmake --preset dev)" >&2
	exit 2
fi

# Print the flags for the target that owns a header. Application headers use
# their matching translation unit when one exists, then the target's main
# translation unit; Scintilla headers use Editor.cxx.
flags_for_header() {
	python3 - "$compile_db" "$1" <<'PY'
import json, shlex, sys
from pathlib import Path
db = json.loads(Path(sys.argv[1]).read_text())
header = Path(sys.argv[2])
if header.parts[0] == "app":
    candidates = [header.with_suffix(".cxx").name]
    candidates.append("WaylandWindow.cxx" if header.name.startswith("Wayland") else "ApplicationEditor.cxx")
else:
    candidates = ["Editor.cxx"]
entry = next(
    e for candidate in candidates
    for e in db
    if e["file"].replace("\\", "/").endswith(f"/{candidate}")
)
parts = shlex.split(entry["command"])
out = []
i = 0
while i < len(parts):
	p = parts[i]
	if i == 0 or p == entry["file"] or any(p.endswith(f"/{candidate}") for candidate in candidates):
		i += 1
		continue
	if p in ("-c", "-o"):
		i += 2
		continue
	out.append(p)
	i += 1
print(" ".join(out))
PY

}

# Every application, Scintilla, and Lexilla-facing production header.
list_default_headers() {
	find app -maxdepth 1 -name '*.h' | sort
	find scintilla/src -maxdepth 1 -name '*.h' | sort
	find scintilla/include -maxdepth 1 -name '*.h' | sort
}

# Production headers modified, staged, or untracked relative to HEAD.
list_changed_headers() {
	{
		git -C "$root" diff --name-only HEAD -- app scintilla/src scintilla/include
		git -C "$root" ls-files --others --exclude-standard -- app scintilla/src scintilla/include
	} | grep -E '^(app|scintilla/src|scintilla/include)/[^/]+\.h$' | sort -u | while IFS= read -r path; do
		[ -f "$path" ] && printf '%s\n' "$path"
	done
}

if [ "${1:-}" = "--changed" ]; then
	headers=$(list_changed_headers)
	shift
elif [ "${1:-}" = "--all-src" ]; then
	headers=$(find scintilla/src -maxdepth 1 -name '*.h' | sort)
	shift
elif [ "$#" -gt 0 ]; then
	headers=$*
else
	headers=$(list_default_headers)
fi

if [ -z "${headers:-}" ]; then
	printf 'no production headers to check\n'
	exit 0
fi

fail=0
ok=0
total=0

for path in $headers; do
	[ -n "$path" ] || continue
	total=$((total + 1))
	if [ ! -f "$path" ]; then
		printf 'MISS %s\n' "$path"
		fail=$((fail + 1))
		continue
	fi
	flags=$(flags_for_header "$path")
	name=$(basename -- "$path")
	# shellcheck disable=SC2086
	if printf '#include "%s"\n' "$name" | clang++ -c -x c++ - -o /tmp/self-contained-hdr.o $flags >/tmp/self-contained-hdr.err 2>&1; then
		printf 'ok   %s\n' "$path"
		ok=$((ok + 1))
	else
		printf 'FAIL %s\n' "$path"
		# First two error lines for triage
		grep -E 'error:' /tmp/self-contained-hdr.err | head -n 2 | sed 's/^/     /'
		fail=$((fail + 1))
	fi
done

printf '\n%d ok, %d fail, %d total\n' "$ok" "$fail" "$total"
if [ "$fail" -gt 0 ]; then
	exit 1
fi
exit 0
