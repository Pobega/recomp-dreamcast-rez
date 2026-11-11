# Rez

Statically recompiled Sega Dreamcast port of **Rez** for PortMaster (`aarch64`).

## Requirements

OpenGL ES 3.0 support is required (`reqs: ["power", "!lowres"]`). Runs across Mali-G31 MP2 (Anbernic H700), PowerVR GE8300 (TrimUI Brick / Smart Pro), Mali-G52 (RK3566), and higher-tier ARM handhelds.

Place a valid Rez V1.003 `.chd` disc image into `ports/rez/gamedata/` (or `ports/rez/disc/`):
- **Japanese Release** (`HDR-0178`):
  `SHA-1: 9711a0d0c21af3685672b9a42c72abf999fc7150`
- **European Release** (`MK-5119250`):
  `SHA-1: 01b7c1cc29a840b141a4ae713037eb1224fac233`

On first launch, PortMaster automatically verifies the `.chd` disc image and reconstructs the game binary (~15 seconds).

## In-Game Recomp Settings

Open **Options -> Recomp Settings** from the main menu to configure:
- **Aspect Ratio**: `4:3`, `3:2`, `16:9`, or `1:1` (auto-detected on first boot)
- **Resolution**: Native handheld resolution or lower performance step-downs (`240p` / `320p` / `360p`)
- **Frame Rate**: `30 fps` or `60 fps`

## Controls

| Action | Handheld Control |
|---|---|
| Targeting | D-Pad / Left Stick |
| Lock-on / Shoot | A / B |
| Overdrive | L1 / L2 / R1 / R2 |
| Camera View | X / Y |
| Pause / Menu | Start |
| Exit Game | Menu + Start or Select + Start |

## Configuration & Saves

- Display and frame rate settings are saved in `conf/rez_recompiled/settings.ini`.
- Custom controller bindings can be edited in `conf/rez_recompiled/bindings.txt`.
- Dreamcast VMU save data is stored in `vmu/vmu_save_A1.bin`.
