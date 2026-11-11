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

export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# Performance profiler: set DREAM_PROFILE=1 to log 1-second interval stats to log.txt.
# Live on-screen HUD toggle: Select + Y (or Menu + Y).
# export DREAM_PROFILE=1

if [ -f "$GAMEDIR/rez.patch" ] || [ ! -x "$GAMEDIR/rez" ] || [ ! -f "$GAMEDIR/extracted/fs/1ST_READ.BIN" ]; then
  chmod +x "$GAMEDIR/tools/patchscript" "$GAMEDIR/tools/rez-patch" 2>/dev/null || true
  export PATCHER_FILE="$GAMEDIR/tools/patchscript"
  export PATCHER_GAME="$(basename "${0%.*}")"
  export PATCHER_TIME="about 15 seconds"
  export controlfolder ESUDO DEVICE_ARCH GAMEDIR
  if [ -f "$controlfolder/utils/patcher.txt" ]; then
    source "$controlfolder/utils/patcher.txt"
    $ESUDO kill -9 $(pidof gptokeyb) 2>/dev/null || true
  else
    pm_message "This port requires the latest version of PortMaster."
    sleep 5
    pm_finish
    exit 1
  fi
  if [ ! -x "$GAMEDIR/rez" ] || [ ! -f "$GAMEDIR/extracted/fs/1ST_READ.BIN" ]; then
    pm_finish
    exit 1
  fi
fi

$GPTOKEYB "rez" &
pm_platform_helper "$GAMEDIR/rez"
./rez

pm_finish
