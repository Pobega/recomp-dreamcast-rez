## Notes

Thanks to United Game Artists for creating Rez and [phobos665](https://github.com/phobos665/recomp-dreamcast-rez) for the Dreamcast static recompilation and `dream-recomp` engine, delivering synesthetic wireframe rail-shooting where every lock-on and shot builds the electronic soundtrack across five evolving cybernetic layers.

To run Rez, convert your original Japanese (`HDR-0178`, CHD SHA-1 `9711a0d0c21af3685672b9a42c72abf999fc7150`) or European (`MK-5119250`, CHD SHA-1 `01b7c1cc29a840b141a4ae713037eb1224fac233`) Dreamcast Rez V1.003 disc to `.chd` (for example with `chdman createcd`) and place it in `ports/rez/gamedata/`. First launch verifies the disc and reconstructs `rez.aarch64` in about 15 seconds.

Open **Options -> Recomp Settings** from the main menu to adjust Aspect Ratio (`4:3`, `3:2`, `16:9`, or `1:1`), Resolution step-downs, and Frame Rate (`30 fps` or `60 fps`). Display settings, controller bindings, and Dreamcast VMU saves are stored in `conf/rez_recompiled/` (`settings.ini`, `bindings.txt`, `vmu_a1.bin`).

## Controls

| Key | Action |
|--|--|
| D-Pad | Move Reticle / Navigate Menus |
| Left Analog | Move Reticle |
| A | Lock-on / Shoot / Confirm |
| B | Overdrive / Cancel |
| X | Lock-on / Shoot |
| Start | Pause / In-Game Menu |
| Select | Controller Bindings Menu |
| Select + Y | Toggle FPS Counter |
| Select + Start | Exit Game |

## Compile

```bash
git clone https://github.com/Pobega/rez-portmaster.git
cd rez-portmaster
git checkout portmaster-gles3
sh tools/toolchain.sh
python3 tools/dc.py setup --disc "/path/to/Rez.chd"
python3 tools/dc.py build -j$(nproc)
```
