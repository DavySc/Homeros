# The C sources package 12 builds (build.mk): tree-sitter's runtime and the
# grammars and queries Neovim bundles, all from the nixpkgs that gives the
# check its Neovim, so tests/test_syntax.c compares like with like.  Only
# sources come from nix: its libraries need a newer glibc than our compiler.
let
  pkgs = import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {};
  nv = pkgs.neovim-unwrapped;
  p = nv.treesitter-parsers;
  ts = pkgs.vimPlugins.nvim-treesitter;
  # nvim-treesitter's built grammars, with their sources kept
  keep = g: g.overrideAttrs (o: {
    postInstall = (o.postInstall or "") + "cp -r src $out/src\n";
  });
  rust = keep ts.builtGrammars.rust;
  glsl = keep ts.builtGrammars.glsl;
  nix = keep ts.builtGrammars.nix;
in
pkgs.runCommandLocal "homeros-syntax-src" { } ''
  mkdir -p $out/c $out/lua $out/markdown
  ln -s ${pkgs.tree-sitter.src} $out/tree-sitter
  tar xzf ${p.c.src} -C $out/c --strip-components=1
  tar xzf ${p.lua.src} -C $out/lua --strip-components=1
  tar xzf ${p.markdown.src} -C $out/markdown --strip-components=1
  mkdir -p $out/queries
  for l in c lua markdown markdown_inline; do ln -s ${nv}/share/nvim/runtime/queries/$l $out/queries/$l; done
  for l in rust glsl nix; do ln -s ${ts}/runtime/queries/$l $out/queries/$l; done
  # Neovim bundles no JSON grammar: tests/test_json.c checks JSON's colours
  # against tree-sitter-json and nvim-treesitter's colours for it
  ln -s ${pkgs.tree-sitter-grammars.tree-sitter-json}/parser $out/json.so
  ln -s ${pkgs.vimPlugins.nvim-treesitter.src}/runtime/queries/json/highlights.scm $out/json.scm
  ln -s ${rust}/src $out/rust
  ln -s ${glsl}/src $out/glsl
  ln -s ${nix}/src $out/nix
  mkdir -p $out/nvim/parser $out/nvim/queries $out/corpus
  ln -s ${rust}/parser $out/nvim/parser/rust.so
  ln -s ${glsl}/parser $out/nvim/parser/glsl.so
  ln -s ${nix}/parser $out/nvim/parser/nix.so
  for l in rust glsl nix; do ln -s ${ts}/runtime/queries/$l $out/nvim/queries/$l; done
  ln -s ${pkgs.ripgrep.src}/crates/printer/src $out/corpus/rust
  # glslang's test shaders (some are meant to fail to compile)
  ln -s ${pkgs.glslang.src}/Test $out/corpus/glsl
  # nixpkgs' writers: shell, Python, Rust and Lua in Nix strings
  ln -s ${pkgs.path}/pkgs/build-support/writers $out/corpus/nix
''
