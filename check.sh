#!/bin/sh
# The local check matrix: configure, build, and test the normal (dev),
# AddressSanitizer (asan), and UndefinedBehaviorSanitizer (ubsan) trees.
# Passing this is part of the definition of done for each reviewable step
# (see AGENTS.md). Each preset keeps its own build directory: build/,
# build-asan/, build-ubsan/.
set -e
for preset in dev asan ubsan; do
	cmake --workflow --preset "$preset"
done
echo "check.sh: dev, asan, and ubsan all passed"
