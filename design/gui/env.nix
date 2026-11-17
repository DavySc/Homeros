let pkgs = import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {};
in pkgs.buildEnv { name = "gui-env"; paths = with pkgs; [
  sdl3 sdl3.dev sdl3-ttf freetype freetype.dev harfbuzz harfbuzz.dev wayland wayland.dev wayland-protocols wayland-scanner
  libxkbcommon libxkbcommon.dev weston pkg-config gcc nerd-fonts.jetbrains-mono fontconfig grim mesa libglvnd ]; extraOutputsToInstall = [ "dev" ]; }
