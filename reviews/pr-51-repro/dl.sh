#!/usr/bin/env bash
# Builds dl.c against the glibc nix's parsers need and runs it on nixpkgs'
# own (unpatched) sql parser and on the one the test's Neovim loads.
# From the repo root after `make compilers`, tools-env/bin on PATH.
S=build/syntax
G=$(dirname "$(ldd "$(readlink -f $S/src/nvim/parser/sql.so)" | awk '/libc.so/ {print $3}')")
U=$(nix build --impure --no-link --print-out-paths --expr 'let pkgs = import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {}; in pkgs.vimPlugins.nvim-treesitter.builtGrammars.sql')
mkdir -p build/rv
gcc -g -O1 -I$S/src/tree-sitter/lib/include -o build/rv/dl reviews/pr-51-repro/dl.c $S/tree-sitter.o -ldl \
  -Wl,--dynamic-linker=$G/ld-linux-x86-64.so.2 -Wl,-rpath=$G || exit 1
for so in $U/parser $S/src/nvim/parser/sql.so; do ./build/rv/dl $so reviews/pr-51-repro/body.sql; done
