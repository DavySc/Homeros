#!/usr/bin/env bash
# Builds the Win32 backend's tests for Windows with zig cc and runs them
# under Wine on a virtual X display, until the Makefile has a rule for it
# (asked of thread 0).  Needs zig (design/env.nix), wine with its X11 driver
# (nixpkgs wineWow64Packages.stable) and xvfb-run.  UBSan traps; ASan has no
# runtime for this target, so it runs without.
set -eu
cd "$(dirname "$0")/../.."
pick() { if ls src/$1/*.c >/dev/null 2>&1; then echo src/$1/*.c; else echo ref/$1.c; fi; }
mkdir -p build/win
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=${WINEPREFIX:-$PWD/build/win/prefix}
for t in tests/test_win*.c; do
	exe=build/win/$(basename "$t" .c).exe
	zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror -Iinclude -O1 -g \
	  -fsanitize=undefined -fsanitize-trap=undefined \
	  -o "$exe" "$t" src/win/*.c $(pick grid) $(pick uni) -luser32 -lgdi32 -ldwmapi
	xvfb-run -a wine "$exe" || { echo "FAIL $(basename "$t" .c)"; exit 1; }
done
