#!/bin/sh
set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT/scripts/common.sh"

SOURCE="$ROOT/third_party/openal-soft"
ARCHIVE="$SOURCE/openal-soft-1.21.1.tar.gz"
CONTAINER_BUILD="$ROOT/scripts/openal/build.sh"
OUT=${OUT:-$ROOT/artifacts/openal-armhf}
IMAGE=${DOCKER_IMAGE:-debian:buster}
PLATFORM=${DOCKER_PLATFORM:-linux/arm64}
INSTALL_DEPS=${INSTALL_DEPS:-1}
EXPECTED_SOURCE_SHA=afe3f0aae719fcdc43f10d7ad7f904d53dc43718dcb1bc207e53c9ed0ba9a45a
EXPECTED_TOOLCHAIN_SHA=ad35e1e19e4741c08efe878909e3cd65b39b736c447b2b089cd9fe74d7bc8f5c

for file in \
    "$ARCHIVE" \
    "$SOURCE/svmm-context-voices.patch" \
    "$SOURCE/svmm-realtime-text-lock.patch" \
    "$SOURCE/svmm-miao-backend.patch" \
    "$SOURCE/svmm-miao/miao.cpp" \
    "$SOURCE/svmm-miao/miao.h" \
    "$CONTAINER_BUILD"
do
    [ -f "$file" ] || fail "missing build input: $file"
done

[ "$(sha256_file "$ARCHIVE")" = "$EXPECTED_SOURCE_SHA" ] ||
    fail 'OpenAL source checksum mismatch'
command -v docker >/dev/null 2>&1 || fail 'Docker is required'

if [ -n "${MIYOO_TOOLCHAIN:-}" ]; then
    TOOLCHAIN=$(CDPATH='' cd -- "$MIYOO_TOOLCHAIN" && pwd)
    TOOLCHAIN_ID=${MIYOO_TOOLCHAIN_ID:-custom}
    TOOLCHAIN_SHA=${MIYOO_TOOLCHAIN_SHA:-unknown}
else
    TOOLCHAIN=$("$ROOT/scripts/fetch-toolchain.sh")
    TOOLCHAIN_ID=shauninman/miyoomini-toolchain-buildroot-aarch64:v0.0.3
    TOOLCHAIN_SHA=$EXPECTED_TOOLCHAIN_SHA
fi

[ -x "$TOOLCHAIN/bin/arm-linux-gnueabihf-g++" ] ||
    fail "ARM compiler not found in $TOOLCHAIN"

mkdir -p "$OUT"
OUT=$(CDPATH='' cd -- "$OUT" && pwd)

docker run --rm --platform "$PLATFORM" \
    -e INSTALL_DEPS="$INSTALL_DEPS" \
    -e TOOLCHAIN_ID="$TOOLCHAIN_ID" \
    -e TOOLCHAIN_SHA="$TOOLCHAIN_SHA" \
    -e WRAPPER_SHA="$(sha256_file "$0")" \
    -e BUILD_SCRIPT_SHA="$(sha256_file "$CONTAINER_BUILD")" \
    -e SOURCE_DATE_EPOCH=1612465746 \
    -v "$TOOLCHAIN":/opt/miyoo-toolchain:ro \
    -v "$SOURCE":/input:ro \
    -v "$CONTAINER_BUILD":/build-openal.sh:ro \
    -v "$OUT":/out \
    "$IMAGE" bash /build-openal.sh

echo "OpenAL build written to $OUT"
