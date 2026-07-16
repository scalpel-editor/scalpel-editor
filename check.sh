#!/bin/sh
# The local check matrix: configure, build, and test the normal (dev),
# AddressSanitizer (asan), and UndefinedBehaviorSanitizer (ubsan) trees.
# This is the phase and release gate, not the routine development loop
# (see AGENTS.md). Each preset keeps its own build directory: build/,
# build-asan/, build-ubsan/.
set -e
for preset in dev asan ubsan; do
	cmake --workflow --preset "$preset"
done
echo "check.sh: dev, asan, and ubsan all passed"
