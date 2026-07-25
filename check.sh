#!/bin/sh
# The local check matrix: configure, build, and test the normal (dev),
# AddressSanitizer (asan), and UndefinedBehaviorSanitizer (ubsan) trees.
# Pass "nixos" to select the parallel NixOS presets and build trees.
# This is the phase and release gate, not the routine development loop
# (see AGENTS.md). Each preset keeps its own build directory: build/,
# build-asan/, build-ubsan/.
set -e
preset_suffix=
if [ "$#" -gt 1 ]; then
	echo "usage: $0 [nixos]" >&2
	exit 2
fi
if [ "$#" -eq 1 ]; then
	if [ "$1" != "nixos" ]; then
		echo "usage: $0 [nixos]" >&2
		exit 2
	fi
	preset_suffix=-nixos
fi
for preset in dev asan ubsan; do
	cmake --workflow --preset "$preset$preset_suffix"
done
echo "check.sh: dev$preset_suffix, asan$preset_suffix, and ubsan$preset_suffix all passed"
