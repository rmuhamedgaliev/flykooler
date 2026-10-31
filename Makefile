# SPDX-License-Identifier: MIT
# Flydigi Kooler — a Make wrapper around CMake for the familiar workflow.
#
#   make                              build (Release)
#   make test                         build and run the unit tests
#   make package                      build a TGZ package (see PKGBUILD for Arch)
#   make install                      install into PREFIX (/usr/local, needs sudo)
#   make install PREFIX=$HOME/.local  install without root
#   make run                          build and run the GUI from build/
#   make clean                        remove object files
#   make distclean                    remove build/ entirely
#   make uninstall                    remove installed files (via install_manifest.txt)

PREFIX ?= /usr/local
BUILD  ?= build

# plain `make` must build, not just configure
.DEFAULT_GOAL := all

CMAKE_CMD := cmake -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles"

.PHONY: all install test uninstall clean distclean run

$(BUILD)/Makefile: CMakeLists.txt
	@$(CMAKE_CMD)

all: $(BUILD)/Makefile
	@$(MAKE) -s -C $(BUILD)

install: $(BUILD)/Makefile
	@$(MAKE) -s -C $(BUILD)
	@cmake --install $(BUILD) --prefix $(PREFIX)
	@echo "==> Installed into $(PREFIX)"
	@if [ "$(PREFIX)" = "/usr/local" ]; then \
	  echo "==> Do not forget the udev rule:"; \
	  echo "    sudo cp $(PREFIX)/share/flykooler/70-flydigi-cooler.rules /etc/udev/rules.d/ && sudo udevadm control --reload && sudo udevadm trigger"; \
	fi

test: $(BUILD)/Makefile
	@$(MAKE) -s -C $(BUILD)
	@cd $(BUILD) && ctest --output-on-failure

uninstall:
	@if [ -f $(BUILD)/install_manifest.txt ]; then \
	  xargs -r rm -f < $(BUILD)/install_manifest.txt; \
	  echo "==> Removed (see $(BUILD)/install_manifest.txt)"; \
	else \
	  echo "==> No $(BUILD)/install_manifest.txt — run make install first"; \
	fi

clean:
	@$(MAKE) -s -C $(BUILD) clean 2>/dev/null || true

distclean:
	rm -rf $(BUILD)

run: all
	@$(BUILD)/flykooler
