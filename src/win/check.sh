#!/usr/bin/env bash
# make win: builds the Win32 backend's tests for Windows with zig cc and
# runs them under Wine on a virtual X display.  Needs zig (design/env.nix), wine with its X11 driver
# (tools.nix) and xvfb-run.  UBSan traps; ASan has no
# runtime for this target, so it runs without.
set -eu
cd "$(dirname "$0")/../.."
. tools/winpass.sh
# without the tools say so in words planted.sh does not count as a caught
# plant, so a missing Wine never passes for a failing test
for t in zig wine xvfb-run; do
	command -v $t >/dev/null || { echo "src/win/check.sh: $t is not on PATH"; exit 2; }
done
pick() { if ls src/$1/*.c >/dev/null 2>&1; then echo src/$1/*.c; else echo ref/$1.c; fi; }
mkdir -p build/win
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=${WINEPREFIX:-$PWD/build/win/prefix}
make -s build/win/stb.o   # the images' decoder (package 21, thread 0's, agreed)
for t in tests/test_win*.c; do
	exe=build/win/$(basename "$t" .c).exe
	zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror -Iinclude -O1 -g \
	  -fsanitize=undefined -fsanitize-trap=undefined \
	  -o "$exe" "$t" src/win/*.c $(pick grid) $(pick uni) $(pick theme) \
	  -I/usr/include/stb src/image/image.c src/file/*.c src/loop/*.c $(pick text) build/win/stb.o -luser32 -lgdi32 -ldwmapi -lshell32
	# Wine's processes still on the display when xvfb-run ends it print a
	# last line ("X connection ... broken"); wait for them, so the test's
	# own summary is last (pass)
	pass 300 "$exe" xvfb-run -a sh -c 'wine "$1"; s=$?; wineserver -w; exit $s' sh || { echo "FAIL $(basename "$t" .c)"; exit 1; }
done
