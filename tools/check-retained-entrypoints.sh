#!/bin/sh
# Phase 4 step 17: audit retained iface entries against named methods and thin Message cases.
# See check-retained-entrypoints.py for the checks.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec python3 "$root/tools/check-retained-entrypoints.py"
