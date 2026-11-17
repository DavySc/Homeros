# The check's tools: design/env.nix's, plus Wine.  C libraries and fonts are
# not here: nix's libraries need a newer glibc than the system compiler links
# (packages 7 and 12, measured), so CI installs them, and the fonts the font
# tests pin, from apt.
let pkgs = import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {};
in {
  tools = pkgs.buildEnv {
    name = "homeros-tools";
    paths = [ (import ./design/env.nix).tools
      pkgs.wineWow64Packages.stable ];   # package 9's tests; the minimal Wine has no X11 driver
    ignoreCollisions = true;
  };
}
