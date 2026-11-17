# The C sources package 12 builds (build.mk): tree-sitter's runtime and the
# grammars and queries Neovim bundles, all from the nixpkgs that gives the
# check its Neovim, so tests/test_syntax.c compares like with like.  Only
# sources come from nix: its libraries need a newer glibc than our compiler.
let
  pkgs = import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {};
  nv = pkgs.neovim-unwrapped;
  p = nv.treesitter-parsers;
in
pkgs.runCommandLocal "homeros-syntax-src" { } ''
  mkdir -p $out/c $out/lua $out/markdown
  ln -s ${pkgs.tree-sitter.src} $out/tree-sitter
  tar xzf ${p.c.src} -C $out/c --strip-components=1
  tar xzf ${p.lua.src} -C $out/lua --strip-components=1
  tar xzf ${p.markdown.src} -C $out/markdown --strip-components=1
  ln -s ${nv}/share/nvim/runtime/queries $out/queries
''
