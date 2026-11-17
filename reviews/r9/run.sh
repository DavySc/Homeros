#!/usr/bin/env bash
# Builds one repro (reviews/r9/<name>.c) against src/win/backend.c the way
# src/win/check.sh builds the tests, and runs it under Wine on Xvfb.
# usage: reviews/r9/run.sh cursor00
set -eu
cd "$(dirname "$0")/../.."
pick() { if ls src/$1/*.c >/dev/null 2>&1; then echo src/$1/*.c; else echo ref/$1.c; fi; }
mkdir -p build/win
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=${WINEPREFIX:-$PWD/build/win/prefix}
zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror -Iinclude -O1 -g \
  -fsanitize=undefined -fsanitize-trap=undefined \
  -o build/win/r9_$1.exe reviews/r9/$1.c src/win/*.c $(pick grid) $(pick uni) -luser32 -lgdi32 -ldwmapi -limm32
xvfb-run -a wine build/win/r9_$1.exe
