#!/bin/sh
# Finding B1: drive (built with drv.mk's drive-san) in a Lake project whose
# path is 99 bytes long.  Usage: long-path.sh DRIVE INFOVIEW_ROOT LAKE_DIR
# (LAKE_DIR holds lakefile.toml and Demo.lean, the oracle's README).
set -eu
L=/tmp/$(printf 'd%.0s' $(seq 1 80))/proj
mkdir -p "$L"; cp "$3/lakefile.toml" "$3/Demo.lean" "$L"
cd "$L"
(sleep 6; echo "g 1 2"; sleep 6) | ASAN_OPTIONS=detect_leaks=0 HOMEROS_INFOVIEW="$2" HOMEROS_OPEN=true \
  XDG_STATE_HOME=$(mktemp -d) timeout 20 "$1" Demo.lean 2>&1 | head -12
pkill -f "lake serve" || true
