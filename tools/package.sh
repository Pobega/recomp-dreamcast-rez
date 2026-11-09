#!/bin/sh
# Assemble build/rez.zip for PortMaster distribution.
set -e
cd "$(dirname "$0")/.."
EXE=build/target/game/rez-recomp
PKG=build/pkg
ZIP=build/rez.zip

[ -x "$EXE" ] || { echo "package: $EXE missing (tools/build.sh)"; exit 2; }

rm -rf "$PKG"
mkdir -p "$PKG/rez/gamedata" "$PKG/rez/disc" "$PKG/rez/licenses"

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
"$STRIP" -o "$PKG/rez/rez" "$EXE"
chmod +x "$PKG/rez/rez"

cp game/rez.toml "$PKG/rez/rez.toml"
cp game/rez-pipelines.txt "$PKG/rez/rez-pipelines.txt"

cp portmaster/rez/port.json "$PKG/rez/port.json"
cp portmaster/rez/README.md "$PKG/rez/README.md"

cp LICENSE "$PKG/rez/licenses/rez-recomp-GPL-2.0.txt"
cp THIRD_PARTY_NOTICES.md "$PKG/rez/licenses/THIRD_PARTY_NOTICES.md"
cp engine/third_party/libchdr/LICENSE.txt "$PKG/rez/licenses/libchdr-BSD-3-Clause.txt"
cp engine/third_party/libchdr/deps/lzma-26.02/LICENSE "$PKG/rez/licenses/lzma.txt"
cp engine/third_party/tomlplusplus/LICENSE "$PKG/rez/licenses/tomlplusplus-MIT.txt"
cp assets/fonts/OFL.txt "$PKG/rez/licenses/NotoSans-OFL-1.1.txt"

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
            # Preserve executable permissions in external_attr
            st = os.stat(path)
            zinfo.external_attr = (st.st_mode & 0xFFFF) << 16
            with open(path, "rb") as f:
                z.writestr(zinfo, f.read())
EOF

echo "package: $ZIP ($(du -h "$ZIP" | cut -f1))"
