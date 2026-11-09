#!/bin/sh
# The aarch64 cross toolchain (aarch64, glibc 2.28, GCC 8.3 runtime), copied out of
# LoveRetro's toolchain image into build/toolchain. The image must already be pulled
# (docker pull ghcr.io/loveretro/tg5040-toolchain:latest); no code from it runs (docker create +
# docker cp). The host's clang and lld compile against its sysroot (cmake/toolchains/aarch64-portmaster.cmake).
set -e
cd "$(dirname "$0")/.."
IMG=ghcr.io/loveretro/tg5040-toolchain:latest
OUT=build/toolchain
if [ -d "$OUT/aarch64-nextui-linux-gnu/libc" ]; then
    echo "toolchain: already present at $OUT"
    exit 0
fi
DOCKER="${DOCKER:-$(command -v docker || command -v podman || true)}"
[ -n "$DOCKER" ] || { echo "toolchain: neither docker nor podman found"; exit 2; }
if ! "$DOCKER" image inspect "$IMG" > /dev/null 2>&1; then
    echo "toolchain: pulling $IMG..."
    "$DOCKER" pull "$IMG"
fi
echo "toolchain: image $("$DOCKER" image inspect "$IMG" --format '{{.Id}}')"
mkdir -p "$OUT.part"
CID=$("$DOCKER" create --pull=never "$IMG" /bin/true)
trap '"$DOCKER" rm "$CID" > /dev/null' EXIT
# Through tar: the image's directories are read-only, and docker cp to a folder makes each one
# before its contents; tar sets a directory's mode after filling it.
"$DOCKER" cp "$CID:/opt/aarch64-nextui-linux-gnu" - | tar -x -C "$OUT.part"
mv "$OUT.part/aarch64-nextui-linux-gnu" "$OUT"
rmdir "$OUT.part"
echo "toolchain: $OUT ($(du -sh "$OUT" | cut -f1))"
