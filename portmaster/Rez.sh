#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/rez"
cd "$GAMEDIR"

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export XDG_CONFIG_HOME="$GAMEDIR/conf"
export XDG_DATA_HOME="$GAMEDIR/conf"
mkdir -p "$GAMEDIR/conf/rez_recompiled" "$GAMEDIR/gamedata" "$GAMEDIR/disc"

export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# Performance profiler: logs 1-second interval stats to log.txt with zero GPU overhead.
# Live on-screen HUD toggle: Select + Y (or Menu + Y).
export DREAM_PROFILE=1

$GPTOKEYB "rez" &
pm_platform_helper "$GAMEDIR/rez"
./rez

pm_finish
