#!/usr/bin/env bash
# Runs extra-plants.txt through tools/planted.sh's loop (from the repo root).
d=$(mktemp -d); cp -r Makefile include ref src tests tools design "$d"/
cp reviews/pr-58-repro/extra-plants.txt "$d/tests/plants/zz.txt"
rm -f "$d"/tests/plants/[a-y]*.txt
(cd "$d" && bash tools/planted.sh); rm -rf "$d"
