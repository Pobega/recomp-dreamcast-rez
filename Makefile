ADB ?= adb
JOBS ?= 8

# NextUI (PORTS.pak) SD card layout (TrimUI Brick / Smart Pro, RG35XXSP / H700, etc.)
NEXTUI_PORTS_DIR ?= /mnt/SDCARD/Roms/Ports (PORTS)
NEXTUI_DOTPORTS_DIR ?= $(NEXTUI_PORTS_DIR)/.ports
NEXTUI_GAMEDIR ?= $(NEXTUI_DOTPORTS_DIR)/rez

STAGE_DIR := build/pkg

.PHONY: all build host package push push-fast logs

all: build

build:
	python3 tools/dc.py build -j$(JOBS)

host:
	python3 tools/dc.py build --host -j$(JOBS)

package:
	python3 tools/dc.py package

# Push a release package (rez.patch + patchscript) to a NextUI device over ADB.
# Preserves gamedata/*.chd, conf/, and vmu/ on the SD card, and clears the old
# unpacked `rez` binary so PortMaster's patcher runs on next launch.
push:
	@if [ ! -f "$(STAGE_DIR)/Rez.sh" ] || [ ! -f "$(STAGE_DIR)/rez/rez.patch" ]; then \
		python3 tools/dc.py package; \
	fi
	@$(ADB) get-state >/dev/null
	@echo "==> Pushing Rez.sh to NextUI Ports directories..."
	$(ADB) push "$(STAGE_DIR)/Rez.sh" "$(NEXTUI_PORTS_DIR)/Rez.sh"
	$(ADB) push "$(STAGE_DIR)/Rez.sh" "$(NEXTUI_DOTPORTS_DIR)/Rez.sh"
	@echo "==> Pushing release files to $(NEXTUI_GAMEDIR)/..."
	$(ADB) shell 'mkdir -p "$(NEXTUI_GAMEDIR)/tools" "$(NEXTUI_GAMEDIR)/licenses" "$(NEXTUI_GAMEDIR)/gamedata"'
	$(ADB) push \
		"$(STAGE_DIR)/rez/README.md" \
		"$(STAGE_DIR)/rez/gameinfo.xml" \
		"$(STAGE_DIR)/rez/port.json" \
		"$(STAGE_DIR)/rez/rez.patch" \
		"$(STAGE_DIR)/rez/rez.toml" \
		"$(STAGE_DIR)/rez/rez-pipelines.txt" \
		"$(NEXTUI_GAMEDIR)/"
	$(ADB) push "$(STAGE_DIR)/rez/tools/patchscript" "$(STAGE_DIR)/rez/tools/rez-patch" "$(NEXTUI_GAMEDIR)/tools/"
	$(ADB) push \
		"$(STAGE_DIR)/rez/licenses/rez-recomp-GPL-2.0.txt" \
		"$(STAGE_DIR)/rez/licenses/THIRD_PARTY_NOTICES.md" \
		"$(STAGE_DIR)/rez/licenses/libchdr-BSD-3-Clause.txt" \
		"$(STAGE_DIR)/rez/licenses/lzma.txt" \
		"$(STAGE_DIR)/rez/licenses/tomlplusplus-MIT.txt" \
		"$(NEXTUI_GAMEDIR)/licenses/"
	@echo "==> Cleaning previous unpacked binary so PortMaster patcher runs on boot..."
	$(ADB) shell 'rm -f "$(NEXTUI_GAMEDIR)/rez" "$(NEXTUI_GAMEDIR)/extracted/fs/1ST_READ.BIN" && rmdir "$(NEXTUI_GAMEDIR)/disc" 2>/dev/null || true; sync'
	@echo "==> Done! Launch Rez on your NextUI device to run the PortMaster patcher."

# Fast dev deploy: pushes the unpatched aarch64 binary directly as `rez` (skipping the 15s patcher)
# while preserving gamedata/*.chd, conf/, and vmu/.
push-fast:
	@$(ADB) get-state >/dev/null
	@echo "==> Pushing Rez.sh to NextUI Ports directories..."
	$(ADB) push "portmaster/Rez.sh" "$(NEXTUI_PORTS_DIR)/Rez.sh"
	$(ADB) push "portmaster/Rez.sh" "$(NEXTUI_DOTPORTS_DIR)/Rez.sh"
	@echo "==> Pushing unpatched aarch64 binary and assets directly to $(NEXTUI_GAMEDIR)/..."
	$(ADB) shell 'mkdir -p "$(NEXTUI_GAMEDIR)/tools" "$(NEXTUI_GAMEDIR)/licenses" "$(NEXTUI_GAMEDIR)/gamedata"'
	$(ADB) push "build/target/game/rez-recomp" "$(NEXTUI_GAMEDIR)/rez"
	$(ADB) push \
		"portmaster/rez/README.md" \
		"portmaster/rez/gameinfo.xml" \
		"portmaster/rez/port.json" \
		"game/rez.toml" \
		"game/rez-pipelines.txt" \
		"$(NEXTUI_GAMEDIR)/"
	$(ADB) push "portmaster/rez/tools/patchscript" "build/target/engine/runtime/rez-patch" "$(NEXTUI_GAMEDIR)/tools/"
	$(ADB) shell 'rm -f "$(NEXTUI_GAMEDIR)/rez.patch" && rmdir "$(NEXTUI_GAMEDIR)/disc" 2>/dev/null || true; sync'
	@echo "==> Done! Launch Rez on your NextUI device."

logs:
	@$(ADB) shell 'cat "$(NEXTUI_GAMEDIR)/patchlog.txt" 2>/dev/null; echo "---"; cat "$(NEXTUI_GAMEDIR)/log.txt" 2>/dev/null'
