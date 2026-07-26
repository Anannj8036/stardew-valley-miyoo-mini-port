#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT/scripts/common.sh"

CACHE=${MIYOO_TOOLCHAIN_CACHE:-$ROOT/artifacts/toolchain}
ARCHIVE="$CACHE/miyoomini-toolchain-v0.0.3.tar.xz"
TOOLCHAIN="$CACHE/miyoomini-toolchain"
URL=https://github.com/shauninman/miyoomini-toolchain-buildroot-aarch64/releases/download/v0.0.3/miyoomini-toolchain.tar.xz
EXPECTED_SHA=ad35e1e19e4741c08efe878909e3cd65b39b736c447b2b089cd9fe74d7bc8f5c

if [ -x "$TOOLCHAIN/bin/arm-linux-gnueabihf-g++" ] &&
        [ -f "$TOOLCHAIN/.archive-sha256" ] &&
        [ "$(cat "$TOOLCHAIN/.archive-sha256")" = "$EXPECTED_SHA" ]; then
    echo "$TOOLCHAIN"
    exit 0
fi

mkdir -p "$CACHE"
if [ ! -f "$ARCHIVE" ]; then
    command -v curl >/dev/null 2>&1 || fail 'curl is required'
    echo 'Downloading the Miyoo Mini toolchain...' >&2
    curl -L --fail --retry 3 -o "$ARCHIVE.part" "$URL"
    mv "$ARCHIVE.part" "$ARCHIVE"
fi

actual_sha=$(sha256_file "$ARCHIVE")
[ "$actual_sha" = "$EXPECTED_SHA" ] || fail "toolchain checksum mismatch: $actual_sha"

tmp="$CACHE/.extract.$$"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
rm -rf "$tmp"
mkdir -p "$tmp"
tar -xJf "$ARCHIVE" -C "$tmp"

extracted="$tmp/miyoomini-toolchain"
[ -x "$extracted/bin/arm-linux-gnueabihf-g++" ] ||
    fail 'toolchain archive does not contain the ARM compiler'

rm -rf "$TOOLCHAIN"
printf '%s\n' "$EXPECTED_SHA" > "$extracted/.archive-sha256"
mv "$extracted" "$TOOLCHAIN"
trap - EXIT HUP INT TERM
rm -rf "$tmp"

echo "$TOOLCHAIN"
