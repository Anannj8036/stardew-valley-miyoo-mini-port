#!/bin/bash
set -euo pipefail

if [[ ${INSTALL_DEPS:-1} == 1 ]]; then
    export DEBIAN_FRONTEND=noninteractive
    printf 'Acquire::Check-Valid-Until false;\n' >/etc/apt/apt.conf.d/99no-check-valid
    printf 'deb [trusted=yes] http://archive.debian.org/debian buster main\n' >/etc/apt/sources.list
    printf 'deb [trusted=yes] http://archive.debian.org/debian-security buster/updates main\n' >>/etc/apt/sources.list
    apt-get update >/dev/null
    apt-get install -y cmake make patch >/dev/null
fi

export PATH=/opt/miyoo-toolchain/bin:$PATH
export TZ=UTC

for command in cmake make patch arm-linux-gnueabihf-g++ \
    arm-linux-gnueabihf-readelf arm-linux-gnueabihf-strip; do
    command -v "$command" >/dev/null || {
        echo "error: $command is required" >&2
        exit 1
    }
done

rm -rf /work/openal
mkdir -p /work/openal/build /out
tar -xzf /input/openal-soft-1.21.1.tar.gz -C /work/openal
SOURCE=/work/openal/openal-soft-1.21.1
BUILD=/work/openal/build
LOG=/out/build.log

patch -d "$SOURCE" -p1 --fuzz=0 </input/svmm-context-voices.patch
patch -d "$SOURCE" -p1 --fuzz=0 </input/svmm-realtime-text-lock.patch
cp /input/svmm-miao/miao.cpp "$SOURCE/alc/backends/miao.cpp"
cp /input/svmm-miao/miao.h "$SOURCE/alc/backends/miao.h"
patch -d "$SOURCE" -p1 --fuzz=0 </input/svmm-miao-backend.patch

cmake -S "$SOURCE" -B "$BUILD" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=arm \
    -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc \
    -DCMAKE_CXX_COMPILER=arm-linux-gnueabihf-g++ \
    -DCMAKE_BUILD_TYPE=Release \
    -DMIAO_INCLUDE_DIR=/opt/miyoo-toolchain/arm-linux-gnueabihf/libc/usr/include \
    -DMIAO_LIBRARY=/opt/miyoo-toolchain/arm-linux-gnueabihf/libc/usr/lib/libmi_ao.so \
    -DMIAO_SYS_LIBRARY=/opt/miyoo-toolchain/arm-linux-gnueabihf/libc/usr/lib/libmi_sys.so \
    -DCAM_OS_WRAPPER_LIBRARY=/opt/miyoo-toolchain/arm-linux-gnueabihf/libc/usr/lib/libcam_os_wrapper.so \
    -DCMAKE_C_FLAGS_RELEASE='-O3 -DNDEBUG -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -ffile-prefix-map=/work=.' \
    -DCMAKE_CXX_FLAGS_RELEASE='-O3 -DNDEBUG -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -ffile-prefix-map=/work=.' \
    -DCMAKE_SHARED_LINKER_FLAGS='-Wl,--as-needed -Wl,--build-id=none' \
    -DALSOFT_UTILS=OFF \
    -DALSOFT_EXAMPLES=OFF \
    -DALSOFT_NO_CONFIG_UTIL=ON \
    -DALSOFT_INSTALL=OFF \
    -DALSOFT_INSTALL_CONFIG=OFF \
    -DALSOFT_INSTALL_HRTF_DATA=OFF \
    -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF \
    -DALSOFT_UPDATE_BUILD_VERSION=OFF \
    -DALSOFT_STATIC_STDCXX=ON \
    -DALSOFT_BACKEND_ALSA=OFF \
    -DALSOFT_BACKEND_OSS=OFF \
    -DALSOFT_BACKEND_SNDIO=OFF \
    -DALSOFT_BACKEND_PORTAUDIO=OFF \
    -DALSOFT_BACKEND_PULSEAUDIO=OFF \
    -DALSOFT_BACKEND_JACK=OFF \
    -DALSOFT_BACKEND_SDL2=OFF \
    -DALSOFT_BACKEND_WAVE=OFF \
    -DALSOFT_BACKEND_MIAO=ON \
    -DALSOFT_REQUIRE_MIAO=ON \
    -DALSOFT_EMBED_HRTF_DATA=OFF >"$LOG" 2>&1

cmake --build "$BUILD" --target OpenAL -- -j2 >>"$LOG" 2>&1

library=$(find "$BUILD" -type f -name libopenal.so.1.21.1 -print -quit)
[[ -n $library ]] || {
    tail -n 80 "$LOG" >&2
    echo 'error: OpenAL library was not produced' >&2
    exit 1
}

strip=arm-linux-gnueabihf-strip
readelf=arm-linux-gnueabihf-readelf
$strip --strip-unneeded -o /out/libopenal.so.1 "$library"

for name in libmi_ao.so libmi_sys.so libcam_os_wrapper.so; do
    source_file="/opt/miyoo-toolchain/arm-linux-gnueabihf/libc/usr/lib/$name"
    $strip --strip-unneeded -o "/out/$name" "$source_file"
done

$readelf -h /out/libopenal.so.1 >/out/elf-header.txt
$readelf -d /out/libopenal.so.1 >/out/dynamic.txt
$readelf --version-info /out/libopenal.so.1 >/out/versions.txt

grep -q 'Class:.*ELF32' /out/elf-header.txt
grep -q 'Machine:.*ARM' /out/elf-header.txt
grep -aFq 'alsoft_svmm_get_miao_stats' /out/libopenal.so.1
grep -aFq 'SVMM MI_AO configured' /out/libopenal.so.1
grep -Eq 'NEEDED.*libmi_ao\.so' /out/dynamic.txt
grep -Eq 'NEEDED.*libmi_sys\.so' /out/dynamic.txt
grep -Eq 'NEEDED.*libcam_os_wrapper\.so' /out/dynamic.txt

if grep -Eq 'GLIBCXX_|CXXABI_' /out/versions.txt ||
        grep -Eq 'NEEDED.*libstdc\+\+' /out/dynamic.txt; then
    echo 'error: OpenAL has a dynamic C++ runtime dependency' >&2
    exit 1
fi
if grep -Eq 'NEEDED.*lib(EGL|GLES|GL|SDL|pulse|pipewire)' /out/dynamic.txt; then
    echo 'error: OpenAL has an unwanted graphics or desktop audio dependency' >&2
    exit 1
fi

too_new=$(grep -o 'GLIBC_[0-9][0-9.]*' /out/versions.txt | sort -Vu |
    awk -F_ '{ split($2, v, "."); if (v[1] > 2 || (v[1] == 2 && v[2] > 28)) print $2 }')
[[ -z $too_new ]] || {
    echo "error: OpenAL requires GLIBC newer than 2.28: $too_new" >&2
    exit 1
}

sha256sum /out/libopenal.so.1 >/out/sha256.txt
{
    echo 'openal_version=1.21.1'
    echo 'openal_commit=ae4eacf147e2c2340cc4e02a790df04c793ed0a9'
    echo "toolchain=$TOOLCHAIN_ID"
    echo "toolchain_sha256=$TOOLCHAIN_SHA"
    echo "wrapper_sha256=$WRAPPER_SHA"
    echo "build_script_sha256=$BUILD_SCRIPT_SHA"
    echo "source_sha256=$(sha256sum /input/openal-soft-1.21.1.tar.gz | awk '{print $1}')"
    for file in svmm-context-voices.patch svmm-realtime-text-lock.patch svmm-miao-backend.patch; do
        echo "$file=$(sha256sum "/input/$file" | awk '{print $1}')"
    done
    echo "miao.cpp=$(sha256sum /input/svmm-miao/miao.cpp | awk '{print $1}')"
    echo "miao.h=$(sha256sum /input/svmm-miao/miao.h | awk '{print $1}')"
    echo "library_sha256=$(sha256sum /out/libopenal.so.1 | awk '{print $1}')"
} >/out/build-info.txt

echo 'OpenAL ARMv7 build passed'
