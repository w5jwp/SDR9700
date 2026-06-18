.PHONY: all release debug clean run install

BUILD_DIR := src/build
GENERATOR := Ninja
BUILD_JOBS := $(shell n=$$(nproc); echo $$(( n / 2 > 0 ? n / 2 : 1 )))

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
	./$(BUILD_DIR)/bin/SDR9700

install:
	mkdir -p ~/.local/share/applications
	mkdir -p ~/.local/share/icons/hicolor/256x256/apps
	mkdir -p ~/.local/share/icons/hicolor/512x512/apps
	sed 's|^Exec=.*|Exec=$(abspath $(BUILD_DIR)/bin/SDR9700)|' resources/sdr9700.desktop \
	    > ~/.local/share/applications/sdr9700.desktop
	chmod 644 ~/.local/share/applications/sdr9700.desktop
	install -m 644 resources/images/icons/sdr9700_app_icon_256x256.png \
	    ~/.local/share/icons/hicolor/256x256/apps/sdr9700.png
	install -m 644 resources/images/icons/sdr9700_app_icon_512x512.png \
	    ~/.local/share/icons/hicolor/512x512/apps/sdr9700.png
	update-desktop-database -q ~/.local/share/applications 2>/dev/null || true
	gtk-update-icon-cache -q -f -t ~/.local/share/icons/hicolor 2>/dev/null || true
	kbuildsycoca6 --noincremental 2>/dev/null || true
