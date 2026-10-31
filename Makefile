# SPDX-License-Identifier: MIT
# Flydigi Kooler — a Make wrapper around CMake for the familiar workflow.
#
#   make                              build (Release)
#   make test                         build and run the unit tests
#   make package                      build a TGZ package (see PKGBUILD for Arch)
#   make release VERSION=0.2.0        test, bump, tag, push and publish a release
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

.PHONY: all install test release uninstall clean distclean run

$(BUILD)/Makefile: CMakeLists.txt
	@$(CMAKE_CMD)

all: $(BUILD)/Makefile
	@$(MAKE) -s -C $(BUILD)

install: $(BUILD)/Makefile
	@$(MAKE) -s -C $(BUILD)
	@cmake --install $(BUILD) --prefix $(PREFIX)
	@echo "==> Installed into $(PREFIX)"
	@echo "==> The udev rule and the systemd user unit are not installed by CMake;"
	@echo "    copy them from data/ (see README, 'Build and install')."

test: $(BUILD)/Makefile
	@$(MAKE) -s -C $(BUILD)
	@cd $(BUILD) && ctest --output-on-failure

# Cutting a release: the changelog entry must exist and the tests must pass.
# Bumps CMakeLists.txt and PKGBUILD, commits, tags, pushes, opens the release
# with that changelog section as the notes.
release:
	@test -n "$(VERSION)" || { echo "Usage: make release VERSION=0.2.0"; exit 1; }
	@echo "$(VERSION)" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$$' || { echo "VERSION must be MAJOR.MINOR.PATCH"; exit 1; }
	@test "$$(git rev-parse --abbrev-ref HEAD)" = "main" || { echo "Release from the main branch"; exit 1; }
	@test -z "$$(git status --porcelain | grep -v ' CHANGELOG.md$$')" || { echo "Commit or stash your changes first"; exit 1; }
	@grep -q "## \[$(VERSION)\]" CHANGELOG.md || { \
	  echo "Add a '## [$(VERSION)] - date' section to CHANGELOG.md first"; exit 1; }
	@git rev-parse -q --verify "refs/tags/v$(VERSION)" >/dev/null && { \
	  echo "v$(VERSION) already exists"; exit 1; } || true
	@$(MAKE) test
	@sed -i 's/^        VERSION [0-9][0-9.]*$$/        VERSION $(VERSION)/' CMakeLists.txt
	@sed -i 's/^pkgver=.*/pkgver=$(VERSION)/; s/^pkgrel=.*/pkgrel=1/' PKGBUILD
	@git add CMakeLists.txt PKGBUILD CHANGELOG.md
	@git commit -q -m "Release v$(VERSION)"
	@git tag -a "v$(VERSION)" -m "Release v$(VERSION)"
	@git push -q && git push -q origin "v$(VERSION)"
	@notes=$$(mktemp); \
	  awk '/^## \[/ { if (f) exit; if (index($$0, "[$(VERSION)]")) f=1; next } f' CHANGELOG.md | sed '/./,$$!d' > "$$notes"; \
	  gh release create "v$(VERSION)" --title "v$(VERSION)" --notes-file "$$notes"; \
	  rm -f "$$notes"
	@echo "==> Released: https://github.com/rmuhamedgaliev/flykooler/releases/tag/v$(VERSION)"

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
