# Building Rez Recompiled

## Linux

Needs CMake 3.24+, Ninja, a C++20 compiler (GCC or Clang), Python 3.11+, SDL2, and OpenGL ES 3.0 development headers and library (`<GLES3/gl3.h>`, `libGLESv2`).

```
python tools/dc.py setup --disc "/path/to/Rez (Japan) (En,Ja,Fr,De,Es,It).chd"
python tools/dc.py build
python tools/dc.py run --window
```

`setup` checks the image (its CHD SHA-1, then all of its data against the CHD's own checksum) and
the boot file's SHA-1. It remembers the image's path, so leave the image where it is. Settings and
the memory card are kept in `~/.config/rez_recompiled/`.

## PortMaster (aarch64)

Build the PC version first (`python tools/dc.py setup` and `python tools/dc.py build`) to generate the translated game C++ and build `dream-translate`.

Needs the cross toolchain (`tools/portmaster/toolchain.sh`), Clang, and LLD. Rez uses a unified SDL2 + OpenGL ES 3.0 renderer with no Vulkan dependency, running across all PortMaster aarch64 devices including Mali-G31 MP2 (Anbernic H700 RGSP) and PowerVR GE8300 (TrimUI Brick).

```sh
tools/portmaster/toolchain.sh
tools/portmaster/build.sh
tools/portmaster/package.sh          # writes build-portmaster/rez.zip
```

Executables built from this repository contain the game's code in
translated form, compiled from the builder's own disc. They still need the player's disc to run.
