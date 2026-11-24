# FreeType draws, HarfBuzz shapes, fontconfig finds the fallback fonts (foot).
CPPFLAGS += $(shell pkg-config --cflags freetype2 harfbuzz fontconfig)
LIBS += $(shell pkg-config --libs freetype2 harfbuzz fontconfig)
LIBS += -lm   # sprite.c's hypot

# The same libraries for Windows (windows.nix), copied out of the store as
# the terminal's libghostty-vt is (src/tools/build.mk)
build/font/windows.lib: src/font/windows.nix $(wildcard src/font/win/*)
	nix build -f $< -o build/font-nix-win
	rm -rf build/font && mkdir -p build/font
	cp -r build/font-nix-win/include build/font/
	cp build/font-nix-win/lib/font.lib $@
	chmod -R u+w build/font
