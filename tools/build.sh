#!/bin/sh
# Cross-build Rez for PortMaster handhelds (aarch64) into build/target.
# Needs:
#  - the toolchain (tools/toolchain.sh)
#  - the host build's translator (build/host/engine/translator/dream-translate)
#  - game/extracted from python tools/dc.py setup
set -e
cd "$(dirname "$0")/.."
B=build/target
TC="$PWD/build/toolchain"
TRANSLATE="$PWD/build/host/engine/translator/dream-translate"

[ -d "$TC/aarch64-nextui-linux-gnu/libc" ] || { echo "portmaster: no toolchain at $TC (tools/toolchain.sh)"; exit 2; }
if [ ! -x "$TRANSLATE" ]; then
    echo "portmaster: building host translator ($TRANSLATE)..."
    cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Release
    ninja -C build/host dream-translate rez-patch
fi
[ -x "$TRANSLATE" ] || { echo "portmaster: translator build failed ($TRANSLATE)"; exit 2; }

mkdir -p "$B"
cmake -S . -B "$B" -G Ninja \
    -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG" -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG" \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/aarch64-portmaster.cmake" \
    -DDREAMCOMP_TOOLCHAIN_ROOT="$TC" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDREAM_RENDERER=ON \
    -DDREAM_TRANSLATE_EXECUTABLE="$TRANSLATE" \
    -DDREAM_GEN_OPT="${GEN_OPT:--Os}" > "$B.configure.log"
ninja -C "$B" "$@" rez_boot rez-patch
echo "portmaster: built $B/game/rez-recomp"
