#!/bin/sh
# Fail when listed Scintilla headers do not compile alone (IWYU prerequisite).
#
# Usage:
#   tools/check-self-contained-headers.sh              # every scintilla/src/*.h and include/*.h
#   tools/check-self-contained-headers.sh path/to/H.h  # one or more headers
#   tools/check-self-contained-headers.sh --all-src    # every scintilla/src/*.h only (legacy alias)
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

# Flags from the Editor.cxx command so -I / -std match the library.
flags=$(python3 - "$compile_db" <<'PY'
import json, shlex, sys
from pathlib import Path
db = json.loads(Path(sys.argv[1]).read_text())
entry = next(e for e in db if e["file"].replace("\\", "/").endswith("/Editor.cxx"))
parts = shlex.split(entry["command"])
out = []
i = 0
while i < len(parts):
	p = parts[i]
	if i == 0 or p.endswith("Editor.cxx"):
		i += 1
		continue
	if p in ("-c", "-o"):
		i += 2
		continue
	out.append(p)
	i += 1
print(" ".join(out))
PY
)

# Default: every production header under scintilla/src and Lexilla-facing include/.
list_default_headers() {
	find scintilla/src -maxdepth 1 -name '*.h' | sort
	find scintilla/include -maxdepth 1 -name '*.h' | sort
}

if [ "${1:-}" = "--all-src" ]; then
	headers=$(find scintilla/src -maxdepth 1 -name '*.h' | sort)
	shift
elif [ "$#" -gt 0 ]; then
	headers=$*
else
	headers=$(list_default_headers)
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
