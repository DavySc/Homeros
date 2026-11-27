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
  cmake = keep ts.builtGrammars.cmake;
  systemverilog = keep ts.builtGrammars.systemverilog;
in
pkgs.runCommandLocal "homeros-syntax-src" { } ''
  mkdir -p $out/c $out/lua $out/markdown
  ln -s ${pkgs.tree-sitter.src} $out/tree-sitter
  tar xzf ${p.c.src} -C $out/c --strip-components=1
  tar xzf ${p.lua.src} -C $out/lua --strip-components=1
  tar xzf ${p.markdown.src} -C $out/markdown --strip-components=1
  mkdir -p $out/queries
  for l in c lua markdown markdown_inline; do ln -s ${nv}/share/nvim/runtime/queries/$l $out/queries/$l; done
  for l in rust glsl nix cmake systemverilog; do ln -s ${ts}/runtime/queries/$l $out/queries/$l; done
  # Neovim bundles no JSON grammar: tests/test_json.c checks JSON's colours
  # against tree-sitter-json and nvim-treesitter's colours for it
  ln -s ${pkgs.tree-sitter-grammars.tree-sitter-json}/parser $out/json.so
  ln -s ${pkgs.vimPlugins.nvim-treesitter.src}/runtime/queries/json/highlights.scm $out/json.scm
  ln -s ${rust}/src $out/rust
  ln -s ${glsl}/src $out/glsl
  ln -s ${nix}/src $out/nix
  ln -s ${cmake}/src $out/cmake
  ln -s ${systemverilog}/src $out/systemverilog
  mkdir -p $out/nvim/parser $out/nvim/queries $out/corpus
  ln -s ${rust}/parser $out/nvim/parser/rust.so
  ln -s ${glsl}/parser $out/nvim/parser/glsl.so
  ln -s ${nix}/parser $out/nvim/parser/nix.so
  ln -s ${cmake}/parser $out/nvim/parser/cmake.so
  ln -s ${systemverilog}/parser $out/nvim/parser/systemverilog.so
  for l in rust glsl nix cmake systemverilog; do ln -s ${ts}/runtime/queries/$l $out/nvim/queries/$l; done
  # Zig is coloured by Zig's own parser (zig.zig); Neovim's side of
  # tests/test_syntax_zig.c is nvim-treesitter's Zig grammar and queries,
  # and the corpus the std of the Zig release nixpkgs builds
  ln -s ${ts.builtGrammars.zig}/parser $out/nvim/parser/zig.so
  ln -s ${ts}/runtime/queries/zig $out/nvim/queries/zig
  ln -s ${pkgs.zig.src}/lib/std $out/corpus/zig
  ln -s ${pkgs.ripgrep.src}/crates/printer/src $out/corpus/rust
  # glslang's test shaders (some are meant to fail to compile)
  ln -s ${pkgs.glslang.src}/Test $out/corpus/glsl
  # nixpkgs' writers: shell, Python, Rust and Lua in Nix strings
  ln -s ${pkgs.path}/pkgs/build-support/writers $out/corpus/nix
  # CMake's own modules, and Verilator's SystemVerilog tests
  mkdir $out/corpus/cmake
  tar xzf ${pkgs.cmake.src} -C $out/corpus/cmake --strip-components=2 --wildcards '*/Modules/*.cmake'
  ln -s ${pkgs.verilator.src}/test_regress/t $out/corpus/systemverilog
''
