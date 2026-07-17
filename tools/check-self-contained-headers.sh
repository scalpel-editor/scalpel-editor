#!/bin/sh
# Fail when listed Scintilla headers do not compile alone (IWYU prerequisite).
#
# Usage:
#   tools/check-self-contained-headers.sh              # default Editor-related set
#   tools/check-self-contained-headers.sh path/to/H.h  # one or more headers
#   tools/check-self-contained-headers.sh --all-src    # every scintilla/src/*.h
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

# Default set: headers the Editor concern DAG uses (see ROADMAP IWYU cleanup).
default_headers='
scintilla/src/Position.h
scintilla/src/CharacterType.h
scintilla/src/UniqueString.h
scintilla/src/CaseFolder.h
scintilla/src/CaseConvert.h
scintilla/src/UniConversion.h
scintilla/src/ElapsedPeriod.h
scintilla/src/Geometry.h
scintilla/src/Platform.h
scintilla/src/SplitVector.h
scintilla/src/Partitioning.h
scintilla/src/RunStyles.h
scintilla/src/CellBuffer.h
scintilla/src/PerLine.h
scintilla/src/CharacterCategoryMap.h
scintilla/src/CharClassify.h
scintilla/src/Decoration.h
scintilla/src/ContractionState.h
scintilla/src/Document.h
scintilla/src/Style.h
scintilla/src/Indicator.h
scintilla/src/LineMarker.h
scintilla/src/ViewStyle.h
scintilla/src/Selection.h
scintilla/src/PositionCache.h
scintilla/src/KeyMap.h
scintilla/src/EditModel.h
scintilla/src/EditView.h
scintilla/src/MarginView.h
scintilla/src/Editor.h
scintilla/src/ScintillaBase.h
scintilla/src/AutoComplete.h
scintilla/src/CallTip.h
scintilla/src/EditorBasicTypes.h
scintilla/src/EditorDocumentTypes.h
scintilla/src/EditorStyleTypes.h
scintilla/src/EditorInputTypes.h
scintilla/src/EditorLayoutTypes.h
scintilla/src/EditorCommands.h
scintilla/src/EditorNotifications.h
scintilla/src/EditorRecording.h
scintilla/include/ILoader.h
scintilla/include/ILexer.h
scintilla/src/Debugging.h
'

if [ "${1:-}" = "--all-src" ]; then
	headers=$(find scintilla/src -maxdepth 1 -name '*.h' | sort)
	shift
elif [ "$#" -gt 0 ]; then
	headers=$*
else
	headers=$default_headers
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
