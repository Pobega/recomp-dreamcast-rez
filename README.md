# Rez Recompiled
Rez (Sega Dreamcast, 2001) is one of my favourite games.
This is a recomp of Rez for Linux and PortMaster handhelds. It has been statically recompiled to C++.
Unlike many recomps, this one does not rely on hardware processor simulation. A large amount of
time has been spent on performance, targeting handhelds and desktop Linux.

<img width="1024" height="768" alt="rez-005" src="https://github.com/user-attachments/assets/53d0e6d9-c97f-4733-afb9-8ba0cb8bdd46" />

<img width="1024" height="768" alt="rez-010" src="https://github.com/user-attachments/assets/d5418f5a-0142-4789-b04d-3334e38d03d8" />

<img width="1024" height="768" alt="rez-012" src="https://github.com/user-attachments/assets/588c3baa-e580-4023-8499-8f2649b6c4c1" />

<img width="1024" height="768" alt="rez-013" src="https://github.com/user-attachments/assets/2067e11c-ba29-4d92-b376-ee98f64e20c7" />

<img width="1024" height="768" alt="rez-034" src="https://github.com/user-attachments/assets/281baad0-005b-49e1-899a-efa077710aad" />

<img width="1024" height="768" alt="rez-035" src="https://github.com/user-attachments/assets/e255606d-0a20-4ed0-ac20-830b8a9aeaba" />

<img width="1024" height="768" alt="rez-040" src="https://github.com/user-attachments/assets/8038c7d9-c633-4f3a-a595-bf27a1aa5742" />

## Current Features
- Plays from the Japanese and European releases (V1.003 .chd).
- Game has been retooled to run at 60hz with optional widescreen.
- In-game Recomp Settings menu: select aspect ratio (`4:3`, `3:2`, `16:9`, `1:1`), native handheld resolutions with performance step-downs, and frame rate mode (`30 fps` or `60 fps`) with automatic HUD and pause menu scaling.
- Linux PC: Unified OpenGL ES 3.0 renderer, SDL2 window, sound and controllers.
- PortMaster (aarch64): Unified OpenGL ES 3.0 architecture running identically on both Mali-G31 MP2 devices (Anbernic H700 RGSP) and PowerVR GE8300 devices (TrimUI Brick) with no Vulkan dependency.

## Install on PortMaster
1. Download or package `rez.zip`.
2. Extract `rez.zip` into your `ports/` directory on the SD card (creating `ports/Rez.sh` and `ports/rez/`).
3. Copy your Rez `.chd` (Japanese or European) to `ports/rez/gamedata/` or `ports/rez/disc/`.
4. Launch Rez from the Ports menu. On first launch, PortMaster's built-in patcher verifies the `.chd` disc image and reconstructs the game executable automatically (~15 seconds).
5. Exit hotkey: `Menu + Start` or `Select + Start`.

## Building
See [BUILDING.md](BUILDING.md).

## Credits
Rez Recompiled is heavily indebted to
- [**dream-recomp**](https://github.com/phobos665/dream-recomp)
- [**dreamcomp**](https://github.com/cherryduck/dreamcomp)
- [**flycast**](https://github.com/flyinghead/flycast)

## Licence

GPL-2.0 ([LICENSE](LICENSE)).
