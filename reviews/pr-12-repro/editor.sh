#!/bin/sh
# Review of PR 12: uni_glen/uni_width against Neovim driven as an editor
# (`l`, virtcol), not through FFI.  usage: editor.sh SEED COUNT (from the repo root)
set -e
mkdir -p build
cc -O2 -Iinclude reviews/pr-12-repro/segs.c src/uni/uni.c -o build/segs
python3 reviews/pr-12-repro/gen_lines.py "$1" "$2" > build/r12.lines
build/segs < build/r12.lines > build/r12.uni
nvim --clean --headless -l reviews/pr-12-repro/editor.lua build/r12.lines > build/r12.nvim
python3 reviews/pr-12-repro/cmp.py build/r12.lines build/r12.uni build/r12.nvim
