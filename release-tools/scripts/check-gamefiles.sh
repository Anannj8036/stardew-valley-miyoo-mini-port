#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <Stardew gamefiles directory>" >&2
    exit 2
fi

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE=$1
. "$ROOT/scripts/common.sh"

# TODO: consider ripping this out if other minor version differences (1.6.x) also work
EXPECTED_GAME_SHA256=505d343f04420186ba2b611bcc5d256eff554451f55a6b37f3454362d5e03656
EXPECTED_XTILE_SHA256=a05a1123aa3abb8c68ec2589649dfac724dd3cc52a2e0d812f04ffab794a7be5
EXPECTED_TREE_SHA256=fdb83eb53853ebd8864899515d2a33942f5fd22ea4025f501ae167488176d9ea
EXPECTED_XNB_COUNT=3550

[ -d "$SOURCE" ] || fail "gamefiles directory does not exist: $SOURCE"
SOURCE=$(CDPATH='' cd -- "$SOURCE" && pwd)

for required in "Stardew Valley.exe" xTile.dll Content; do
    [ -e "$SOURCE/$required" ] || fail "missing game file: $required"
done

symlink=$(find "$SOURCE" -type l -print -quit)
[ -z "$symlink" ] || fail "gamefiles contain a symbolic link: $symlink"

generated=$(find "$SOURCE" -type f \( \
    -name '*.svtex' -o \
    -name 'Stardew Valley.XmlSerializers.dll' -o \
    -name 'MonoGame.Framework.XmlSerializers.dll' -o \
    -name 'mscorlib.XmlSerializers.dll' -o \
    -name 'SVMM.MapRuntime.dll' -o \
    -name 'xTile.dll.so' -o \
    -name 'Stardew Valley.XmlSerializers.dll.so' \
    \) -print -quit)
[ -z "$generated" ] || fail "remove files left by an earlier setup: $generated"

game_sha256=$(sha256_file "$SOURCE/Stardew Valley.exe")
xtile_sha256=$(sha256_file "$SOURCE/xTile.dll")
xnb_count=$(find "$SOURCE/Content" -type f -name '*.xnb' -print | wc -l | tr -d ' ')
tree_sha256=$(hash_tree "$SOURCE")

if [ "$game_sha256" != "$EXPECTED_GAME_SHA256" ]; then
    fail "unsupported Stardew Valley.exe; expected the 1.6.14.24317 compatibility build"
fi
if [ "$xtile_sha256" != "$EXPECTED_XTILE_SHA256" ]; then
    fail 'unsupported xTile.dll'
fi
if [ "$xnb_count" != "$EXPECTED_XNB_COUNT" ]; then
    fail "incomplete Content directory: found $xnb_count XNB files, expected $EXPECTED_XNB_COUNT"
fi
if [ "$tree_sha256" != "$EXPECTED_TREE_SHA256" ]; then
    fail 'gamefiles do not match the supported compatibility release'
fi

echo "Game files verified: $SOURCE"
