#!/bin/sh
set -eu
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
GAMEFILES=${1:-$ROOT/gamefiles}
OUTPUT=${2:-$ROOT/OnionOS-package}
"$ROOT/scripts/prepare-game.sh" "$GAMEFILES" "$OUTPUT"
