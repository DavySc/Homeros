# FreeType draws, HarfBuzz shapes, fontconfig finds the fallback fonts (foot).
CPPFLAGS += $(shell pkg-config --cflags freetype2 harfbuzz fontconfig)
LIBS += $(shell pkg-config --libs freetype2 harfbuzz fontconfig)
