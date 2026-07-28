.PHONY: all release debug clean run bundle sign dmg release-dmg notarize install

BUILD_DIR := src/build
GENERATOR := Ninja
BUILD_JOBS := $(shell \
	if command -v getconf >/dev/null 2>&1; then n=$$(getconf _NPROCESSORS_ONLN); \
	elif command -v nproc >/dev/null 2>&1; then n=$$(nproc); \
	else n=1; fi; \
	echo $$(( n / 2 > 0 ? n / 2 : 1 )))

all: release

release:
	rm -rf $(BUILD_DIR)
	cmake -S . -B $(BUILD_DIR) -G $(GENERATOR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j$(BUILD_JOBS)

debug:
	rm -rf $(BUILD_DIR)
	cmake -S . -B $(BUILD_DIR) -G $(GENERATOR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) -j$(BUILD_JOBS)

clean:
	rm -rf $(BUILD_DIR)

run:
	@if [ "$$(uname -s)" = "Darwin" ]; then \
	    open $(BUILD_DIR)/bin/SDR9700.app; \
	else \
	    ./$(BUILD_DIR)/bin/SDR9700; \
	fi

bundle:
	@if [ "$$(uname -s)" != "Darwin" ]; then \
	    echo "The bundle target is only available on macOS."; \
	    exit 1; \
	fi
	./resources/macos/scripts/deploy-macos.sh $(BUILD_DIR)/bin/SDR9700.app

sign:
	./resources/macos/scripts/sign-macos.sh $(BUILD_DIR)/bin/SDR9700.app

dmg: bundle
	./resources/macos/scripts/package-macos.sh $(BUILD_DIR)/bin/SDR9700.app $(BUILD_DIR)/package

release-dmg: bundle sign
	./resources/macos/scripts/package-macos.sh $(BUILD_DIR)/bin/SDR9700.app $(BUILD_DIR)/package

notarize:
	@if [ -z "$(DMG)" ]; then \
	    echo "Usage: make notarize DMG=src/build/package/SDR9700-<version>-macOS-apple-silicon.dmg"; \
	    exit 1; \
	fi
	./resources/macos/scripts/notarize-macos.sh "$(DMG)"

install:
	@if [ "$$(uname -s)" = "Darwin" ]; then \
	    echo "The macOS install/package target is not implemented yet."; \
	    exit 1; \
	else \
	    mkdir -p ~/.local/share/applications; \
	    mkdir -p ~/.local/share/icons/hicolor/256x256/apps; \
	    mkdir -p ~/.local/share/icons/hicolor/512x512/apps; \
	    sed 's|^Exec=.*|Exec=$(abspath $(BUILD_DIR)/bin/SDR9700)|' resources/linux/sdr9700.desktop \
	        > ~/.local/share/applications/sdr9700.desktop; \
	    chmod 644 ~/.local/share/applications/sdr9700.desktop; \
	    install -m 644 resources/images/icons/sdr9700_app_icon_256x256.png \
	        ~/.local/share/icons/hicolor/256x256/apps/sdr9700.png; \
	    install -m 644 resources/images/icons/sdr9700_app_icon_512x512.png \
	        ~/.local/share/icons/hicolor/512x512/apps/sdr9700.png; \
	    update-desktop-database -q ~/.local/share/applications 2>/dev/null || true; \
	    gtk-update-icon-cache -q -f -t ~/.local/share/icons/hicolor 2>/dev/null || true; \
	    kbuildsycoca6 --noincremental 2>/dev/null || true; \
	fi
