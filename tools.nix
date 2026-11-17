# The check's tools: design/env.nix's, plus the pinned fonts the font tests
# use (package 7; they find them in tools-env/share/fonts) and Wine.  C libraries are
# not here: nix's need a newer glibc than the system compiler links (CI
# installs them from apt).
let pkgs = import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {};
in {
  tools = pkgs.buildEnv {
    name = "homeros-tools";
    paths = [ (import ./design/env.nix).tools ] ++ (with pkgs; [
      wineWow64Packages.stable   # package 9's tests; the minimal Wine has no X11 driver
      nerd-fonts.jetbrains-mono noto-fonts noto-fonts-cjk-sans noto-fonts-color-emoji dejavu_fonts ]);
    ignoreCollisions = true;
  };
}
