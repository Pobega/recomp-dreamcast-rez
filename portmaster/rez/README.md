# Rez

Dreamcast statically recompiled port for PortMaster.

## Requirements

OpenGL ES 3.0 support is required (`reqs: ["power", "!lowres"]`). Runs across Mali-G31 MP2 and PowerVR GE8300 devices.

Place a valid CHD image into `ports/rez/gamedata/` or `ports/rez/disc/`:
- **Japanese Release** (`HDR-0178`):
  `SHA-1: 9711a0d0c21af3685672b9a42c72abf999fc7150`
- **European Release** (`MK-5119250`):
  `SHA-1: 01b7c1cc29a840b141a4ae713037eb1224fac233`

On first launch, PortMaster automatically verifies the `.chd` disc image and reconstructs the game binary (~15 seconds).

## Controls

| Action | Handheld Control |
|---|---|
| Lock-on / Shoot | A / B |
| Overdrive | L1 / L2 / R1 / R2 |
| Targeting | D-Pad / Left Stick |
| Pause / Menu | Start |
| Exit Game | Menu + Start or Select + Start |

## Configuration

Settings are saved in `conf/rez_recompiled/settings.ini`.
Custom controller bindings can be edited in `conf/rez_recompiled/bindings.txt`.
