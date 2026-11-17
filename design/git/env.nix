let pkgs = import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {};
in (pkgs.emacsPackagesFor pkgs.emacs-nox).emacsWithPackages (e: [ e.magit e.evil e.evil-collection ])
