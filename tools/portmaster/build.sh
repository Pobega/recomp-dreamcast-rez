#!/bin/sh
# Cross-build Rez for PortMaster handhelds (aarch64) into build-portmaster/rez.
# Needs:
#  - the toolchain (tools/portmaster/toolchain.sh)
#  - the PC build's translator (python tools/dc.py build)
#  - game/extracted from python tools/dc.py setup
set -e
cd "$(dirname "$0")/../.."
B=build-portmaster/rez
if [ -d "$PWD/build-portmaster/toolchain/aarch64-nextui-linux-gnu/libc" ]; then
    TC="$PWD/build-portmaster/toolchain"
else
    TC="$PWD/build-brick/toolchain"
fi
TRANSLATE="$PWD/build/engine/translator/dream-translate"
[ -d "$TC/aarch64-nextui-linux-gnu/libc" ] || { echo "portmaster: no toolchain at $TC (tools/portmaster/toolchain.sh)"; exit 2; }
if [ ! -x "$TRANSLATE" ]; then
    echo "portmaster: building host translator ($TRANSLATE)..."
    python3 tools/dc.py build --target dream-translate
fi
[ -x "$TRANSLATE" ] || { echo "portmaster: translator build failed ($TRANSLATE)"; exit 2; }

cmake -S . -B "$B" -G Ninja \
    -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG" -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG" \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/tg5040.cmake" \
    -DDREAMCOMP_TG5040_ROOT="$TC" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDREAMCOMP_PORTMASTER=ON \
    -DDREAMCOMP_NEXTUI=OFF \
    -DDREAM_RENDERER=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON \
    -DDREAM_TRANSLATE_EXECUTABLE="$TRANSLATE" \
    -DDREAM_GEN_OPT="${GEN_OPT:--O1}" > "$B.configure.log"
ninja -C "$B" "$@" rez_boot
echo "portmaster: built $B/game/rez-recomp"
