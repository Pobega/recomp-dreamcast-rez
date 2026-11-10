# Building Rez Recompiled (PortMaster aarch64)

Rez Recompiled targets Linux handhelds running PortMaster (`aarch64`). It cross-compiles using Clang, LLD, and the extracted sysroot toolchain, rendering via a unified OpenGL ES 3.0 pipeline with SDL2.

## Prerequisites

- CMake 3.24+, Ninja, Clang, LLD, and Python 3.11+
- Podman or Docker (to extract the cross-toolchain sysroot)
- A supported Rez Dreamcast disc image (Japanese or European `.chd`)

## Build Steps

1. **Extract the cross-compilation toolchain**:
   ```sh
   sh tools/toolchain.sh
   ```
   This extracts the `aarch64` sysroot into `build/toolchain/`.

2. **Verify disc and extract boot code**:
   ```sh
   python3 tools/dc.py setup --disc "/path/to/Rez (Japan) (En,Ja,Fr,De,Es,It).chd"
   ```
   This builds `libchdr` in `build/host/`, validates the CHD checksum, and extracts `1ST_READ.BIN` and overlays into `game/extracted/fs/`.

3. **Build and Package**:
   ```sh
   python3 tools/dc.py build -j8
   # or manually:
   sh tools/build.sh -j8
   sh tools/package.sh
   ```
   This builds the host translator in `build/host/`, cross-compiles `rez_boot` into `build/target/game/rez-recomp`, and packages `build/rez.zip` containing `Rez.sh`, `rez/tools/rez-patch`, `rez/tools/patchscript`, and the disc-keyed `rez/rez.patch` payload.

4. **Optional Linux Desktop Test Build (`x86_64`)**:
   Because the unified OpenGL ES 3.0 + SDL2 renderer also compiles natively on Linux desktop (with `SDL2` and `GLESv2` headers/libs installed), you can build and run a local test binary in `build/host/`:
   ```sh
   python3 tools/dc.py build --host -j8
   python3 tools/dc.py run
   ```

Executables built from this repository contain the game's code in translated form, compiled from the builder's own disc. They still need the player's disc to run.
