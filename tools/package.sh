#!/bin/sh
# Assemble build/rez.zip for PortMaster distribution with disc-keyed rez.patch.
set -e
cd "$(dirname "$0")/.."
EXE=build/target/game/rez-recomp
TARGET_PATCH=build/target/engine/runtime/rez-patch
HOST_PATCH=build/host/engine/runtime/rez-patch
BOOT_BIN=game/extracted/fs/1ST_READ.BIN
PKG=build/pkg
ZIP=build/rez.zip

[ -x "$EXE" ] || { echo "package: $EXE missing (tools/build.sh)"; exit 2; }
[ -x "$TARGET_PATCH" ] || { echo "package: $TARGET_PATCH missing (tools/build.sh)"; exit 2; }
[ -f "$BOOT_BIN" ] || { echo "package: $BOOT_BIN missing (tools/dc.py setup)"; exit 2; }
if [ ! -x "$HOST_PATCH" ]; then
    echo "package: building host rez-patch ($HOST_PATCH)..."
    ninja -C build/host rez-patch
fi
[ -x "$HOST_PATCH" ] || { echo "package: host rez-patch build failed"; exit 2; }

rm -rf "$PKG"
mkdir -p "$PKG/rez/gamedata" "$PKG/rez/disc" "$PKG/rez/licenses" "$PKG/rez/tools"
[ -f portmaster/rez/gamedata/.gitkeep ] && cp portmaster/rez/gamedata/.gitkeep "$PKG/rez/gamedata/.gitkeep"
[ -f portmaster/rez/disc/.gitkeep ] && cp portmaster/rez/disc/.gitkeep "$PKG/rez/disc/.gitkeep"

cp portmaster/Rez.sh "$PKG/Rez.sh"
chmod +x "$PKG/Rez.sh"

STRIP=""
for cand in llvm-strip "$PWD/build/toolchain/bin/aarch64-nextui-linux-gnu-strip" aarch64-linux-gnu-strip; do
    if command -v "$cand" >/dev/null 2>&1 || [ -x "$cand" ]; then
        STRIP="$cand"
        break
    fi
done
[ -n "$STRIP" ] || { echo "package: no compatible aarch64 strip tool found"; exit 3; }

# Bundle target rez-patch and patchscript
"$STRIP" -o "$PKG/rez/tools/rez-patch" "$TARGET_PATCH"
chmod +x "$PKG/rez/tools/rez-patch"
cp portmaster/rez/tools/patchscript "$PKG/rez/tools/patchscript"
chmod +x "$PKG/rez/tools/patchscript"

# Encrypt stripped target executable with disc-keyed stream cipher
"$STRIP" -o build/rez.stripped "$EXE"
gzip -9 -n -c build/rez.stripped > build/rez.gz
"$HOST_PATCH" --crypt "$BOOT_BIN" build/rez.gz "$PKG/rez/rez.patch"
rm -f build/rez.stripped build/rez.gz

cp game/rez.toml "$PKG/rez/rez.toml"
cp game/rez-pipelines.txt "$PKG/rez/rez-pipelines.txt"

cp portmaster/rez/port.json "$PKG/rez/port.json"
cp portmaster/rez/README.md "$PKG/rez/README.md"
[ -f portmaster/rez/gameinfo.xml ] && cp portmaster/rez/gameinfo.xml "$PKG/rez/gameinfo.xml"
[ -f portmaster/rez/screenshot.png ] && cp portmaster/rez/screenshot.png "$PKG/rez/screenshot.png"

cp LICENSE "$PKG/rez/licenses/rez-recomp-GPL-2.0.txt"
cp THIRD_PARTY_NOTICES.md "$PKG/rez/licenses/THIRD_PARTY_NOTICES.md"
cp engine/third_party/libchdr/LICENSE.txt "$PKG/rez/licenses/libchdr-BSD-3-Clause.txt"
cp engine/third_party/libchdr/deps/lzma-26.02/LICENSE "$PKG/rez/licenses/lzma.txt"
cp engine/third_party/tomlplusplus/LICENSE "$PKG/rez/licenses/tomlplusplus-MIT.txt"

rm -f "$ZIP"
python3 - "$PKG" "$ZIP" <<'EOF'
import os, sys, zipfile

src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(src):
        dirs.sort()
        if root != src and not files and not dirs:
            rel = os.path.relpath(root, src) + "/"
            z.write(root, rel)
        for name in sorted(files):
            path = os.path.join(root, name)
            rel = os.path.relpath(path, src)
            zinfo = zipfile.ZipInfo.from_file(path, rel)
            if name == "rez.patch":
                zinfo.compress_type = zipfile.ZIP_STORED
            else:
                zinfo.compress_type = zipfile.ZIP_DEFLATED
            # Preserve executable permissions in external_attr
            st = os.stat(path)
            zinfo.external_attr = (st.st_mode & 0xFFFF) << 16
            with open(path, "rb") as f:
                z.writestr(zinfo, f.read())
EOF

echo "package: $ZIP ($(du -h "$ZIP" | cut -f1))"
