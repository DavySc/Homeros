# The C sources package 12 builds (build.mk): tree-sitter's runtime and the
# grammars and queries Neovim bundles, all from the nixpkgs that gives the
# check its Neovim, so tests/test_syntax.c compares like with like.  Only
# sources come from nix: its libraries need a newer glibc than our compiler.
#
# Languages Neovim does not bundle come from nvim-treesitter, as in my-nvim:
# its queries, and its grammar built as nixpkgs builds it, keeping the
# generated C, so that nvim/ (the test's Neovim loads it) holds the parser
# compiled from the same C.  corpus/ holds real files of each such language
# for test_syntax's random cases.
let
  pkgs = import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {};
  nv = pkgs.neovim-unwrapped;
  p = nv.treesitter-parsers;
  ts = pkgs.vimPlugins.nvim-treesitter;
  g = l: patch: ts.builtGrammars.${l}.overrideAttrs (o: {
    postPatch = (o.postPatch or "") + patch;
    postInstall = (o.postInstall or "") + "cp -r src $out/src\n";
  });
  # its scanner returns without freeing the tag it read when that tag
  # equals the open one (LeakSanitizer, test_syntax SEED=2 CASES=300)
  sql = g "sql" ''
    substituteInPlace src/scanner.c --replace-fail \
      'strcmp(state->start_tag, start_tag) == 0) {' \
      'strcmp(state->start_tag, start_tag) == 0) { free(start_tag);'
  '';
in
pkgs.runCommandLocal "homeros-syntax-src" { } ''
  mkdir -p $out/c $out/lua $out/markdown $out/queries $out/nvim/parser $out/nvim/queries $out/corpus
  ln -s ${pkgs.tree-sitter.src} $out/tree-sitter
  tar xzf ${p.c.src} -C $out/c --strip-components=1
  tar xzf ${p.lua.src} -C $out/lua --strip-components=1
  tar xzf ${p.markdown.src} -C $out/markdown --strip-components=1
  for l in c lua markdown markdown_inline; do ln -s ${nv}/share/nvim/runtime/queries/$l $out/queries/$l; done
  ln -s ${sql}/src $out/sql
  ln -s ${sql}/parser $out/nvim/parser/sql.so
  for l in sql; do ln -s ${ts}/runtime/queries/$l $out/queries/$l; ln -s ${ts}/runtime/queries/$l $out/nvim/queries/$l; done
  ln -s ${pkgs.postgresql.src}/src/test/regress/sql $out/corpus/sql
''
