# zig cc's arguments for the font module on Windows (build.mk's
# build/font/windows.lib, dwrite.c's DirectWrite and HarfBuzz's C++
# runtime), for the Windows builds that draw: src/win/check.sh, wincc
FONT_WIN="-Ibuild/font/include -Ibuild/font/include/harfbuzz build/font/windows.lib -ldwrite -lc++"
