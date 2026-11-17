let pkgs = import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {};
in {
  tools = pkgs.buildEnv { name = "ned-tools"; paths = with pkgs; [ helix neovim emacs-nox tlaplus lean4 cbmc frama-c alloy6 hyperfine tmux zig tree-sitter python3Packages.pyte z3 cvc5 why3 valgrind ]; ignoreCollisions = true; };
}
