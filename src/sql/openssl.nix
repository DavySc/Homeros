# OpenSSL for the Windows build (Davy's plan, 2026-11-21: OpenSSL on every
# system), static, compiled by zig cc as the rest of the Windows build;
# nixpkgs' own cross build of it does not build (its mingw headers fail to
# configure, 2026-11-23).  Only the libraries: no apps, tests, docs,
# engines or loadable modules; no assembly, so zig's assembler is not asked.
{ pkgs ? import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {} }:
pkgs.stdenvNoCC.mkDerivation {
  name = "openssl-${pkgs.openssl.version}-x86_64-windows-gnu";
  src = pkgs.openssl.src;
  nativeBuildInputs = [ pkgs.zig pkgs.perl ];
  dontFixup = true;
  configurePhase = ''
    export ZIG_GLOBAL_CACHE_DIR=$TMPDIR/zc HOME=$TMPDIR
    perl Configure mingw64 no-shared no-asm no-tests no-docs no-apps no-module no-engine no-dso --prefix=$out --libdir=lib \
      CC="zig cc -target x86_64-windows-gnu" AR="zig ar" RANLIB="zig ranlib" RC="zig rc"
  '';
  buildPhase = "make -j$NIX_BUILD_CORES build_libs";
  installPhase = "make install_dev";
}
