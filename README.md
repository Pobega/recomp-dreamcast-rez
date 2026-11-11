# Rez Recompiled (PortMaster OpenGL ES 3.0)

A static C++ recompilation of **Rez** (Sega Dreamcast, 2001) tailored for **PortMaster** Linux handhelds (`aarch64`) and Linux desktop (`x86_64`).

Forked from [phobos665/recomp-dreamcast-rez](https://github.com/phobos665/recomp-dreamcast-rez), this port replaces the desktop Vulkan renderer with a lightweight **OpenGL ES 3.0** rendering pipeline, adds multi-aspect handheld display ladders, and integrates PortMaster's first-boot disc verification and patching workflow.

## Highlights

- **Static SH-4 Recompilation**: Translates the original Dreamcast binaries (`1ST_READ.BIN` and stage overlays) ahead-of-time into native C++ with no CPU interpreter or JIT overhead.
- **Unified OpenGL ES 3.0 Renderer**: Built for embedded handheld GPUs across **Mali-G31 MP2** (Anbernic H700 family), **PowerVR GE8300** (TrimUI Brick / Smart Pro), **Mali-G52** (RK3566), and **Adreno**, with no Vulkan requirement.
- **Clean Room / Disc-Keyed Packaging**: Release packages (`rez.zip`) ship zero Dreamcast executable bytes. On first launch, PortMaster's built-in patcher (`patcher.txt`) verifies the user's `.chd` disc image, extracts `1ST_READ.BIN`, and reconstructs the `rez` executable in ~15 seconds.
- **In-Game Recomp Settings (`Options -> Recomp Settings`)**:
  - **Aspect Ratio**: `4:3`, `3:2`, `16:9` (`Hor+` widescreen FOV), and `1:1` (`Vert+` square handheld FOV), with automatic 2D HUD and pause-menu un-stretching and first-boot screen auto-detection.
  - **Resolution Ladders**: Native handheld resolutions plus sub-native performance step-downs for each aspect ratio:
    - **`4:3`**: `320x240`, `480x360`, `640x480`, `800x600`, `960x720`, `1024x768`, `1280x960`
    - **`3:2`**: `360x240`, `480x320`, `540x360`, `720x480`, `960x640`, `1080x720`
    - **`16:9`**: `426x240`, `640x360`, `854x480`, `960x544`, `1280x720`, `1920x1080`
    - **`1:1`**: `240x240`, `360x360`, `480x480`, `540x540`, `720x720`
  - **Frame Rate**: Toggle between `30 fps` (original Dreamcast pacing) and `60 fps` (60 Hz logic and rendering) on the fly.

## Supported Disc Images

You must supply your own Dreamcast **Rez** (V1.003) `.chd` disc image:

| Region | Product ID | Version | CHD SHA-1 |
|---|---|---|---|
| **Japan** | `HDR-0178` | `V1.003` | `9711a0d0c21af3685672b9a42c72abf999fc7150` |
| **Europe** | `MK-5119250` | `V1.003` | `01b7c1cc29a840b141a4ae713037eb1224fac233` |

## Installing on PortMaster

1. Download `rez.zip` from the [Releases](https://github.com/Pobega/recomp-dreamcast-rez/releases) page (or build it via `python3 tools/dc.py package`).
2. Extract `rez.zip` into your SD card's `ports/` directory (creating `ports/Rez.sh` and `ports/rez/`).
3. Copy your Rez `.chd` disc image into `ports/rez/gamedata/`.
4. Launch **Rez** from PortMaster. On first boot, PortMaster's patcher screen will verify your `.chd` and build the executable (~15 seconds).

## Controls

| Action | Handheld Control | Desktop Keyboard |
|---|---|---|
| **Targeting** | D-Pad / Left Stick | Arrow Keys / WASD |
| **Lock-on / Shoot** | A / B | Space / Z / Enter |
| **Overdrive** | L1 / L2 / R1 / R2 | X / Left Shift |
| **Camera View** | X / Y | C / V |
| **Pause / Menu** | Start | Escape / Return |
| **Exit Game** | `Menu + Start` or `Select + Start` | `Alt + F4` |

## Configuration & Save Data

- **In-Game Display & FPS Settings**: Persisted automatically to `ports/rez/conf/rez_recompiled/settings.ini`.
- **Controller Bindings**: Customizable in `ports/rez/conf/rez_recompiled/bindings.txt`.
- **VMU Save Data**: Stored in `ports/rez/vmu/vmu_save_A1.bin`.

## Building from Source

See [BUILDING.md](BUILDING.md) for instructions on setting up the `aarch64` sysroot toolchain, building for PortMaster or Linux desktop, and generating the `rez.zip` release archive.

## Credits

- [**phobos665 / dream-recomp**](https://github.com/phobos665/recomp-dreamcast-rez) — Original Rez static recompilation and `dream-recomp` engine
- [**dreamcomp**](https://github.com/cherryduck/dreamcomp) — Dreamcast recompilation research and foundation
- [**flycast**](https://github.com/flyinghead/flycast) — Dreamcast hardware and Holly/AICA reference

## License

GPL-2.0 — see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
