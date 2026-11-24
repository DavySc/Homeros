# FreeType 2.14.3 and HarfBuzz 14.5.1 for the Windows build (dwrite.c finds
# the fonts, these draw and shape them, as on Linux), built with zig cc for
# x86_64-windows-gnu into one static library, as WezTerm links them.  The
# sources are nixpkgs' own (same URLs and hashes, so cache.nixos.org has
# them).  FreeType gets only the modules Windows' fonts need (modules.h);
# HarfBuzz is built without threads (HB_NO_MT), as Homeros has one.
{ pkgs ? import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {} }:
let
  ft = pkgs.fetchurl { url = "mirror://savannah/freetype/freetype-2.14.3.tar.xz"; hash = "sha256-NrxPHMQTM1No7mVsQq/KZcWjmH6HaMwozxG6d154Wl8="; };
  hb = pkgs.fetchurl { url = "https://github.com/harfbuzz/harfbuzz/releases/download/14.5.1/harfbuzz-14.5.1.tar.xz"; hash = "sha256-fi+k6MfJjo2BQGcfV3JUKvqqaszPvXRlBohrbYX3+NY="; };
in pkgs.stdenvNoCC.mkDerivation {
  name = "homeros-font-windows";
  srcs = [ ft hb ];
  sourceRoot = ".";
  nativeBuildInputs = [ pkgs.zig ];
  dontConfigure = true;
  dontFixup = true;
  buildPhase = ''
    export ZIG_GLOBAL_CACHE_DIR=$TMPDIR/zc HOME=$TMPDIR
    T="-target x86_64-windows-gnu -O2"
    F=freetype-2.14.3
    for m in base/ftsystem base/ftinit base/ftdebug base/ftbase base/ftbitmap base/ftglyph base/ftmm base/ftbbox \
             gzip/ftgzip autofit/autofit truetype/truetype cff/cff psaux/psaux pshinter/pshinter psnames/psnames sfnt/sfnt smooth/smooth; do
      zig cc $T -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H="modules.h"' -I${./win} -I$F/include -c $F/src/$m.c -o ft_$(basename $m).o
    done
    zig c++ $T -fno-exceptions -fno-rtti -DHB_NO_MT -c harfbuzz-14.5.1/src/harfbuzz.cc -o hb.o
    zig ar rcs font.lib ft_*.o hb.o
  '';
  installPhase = ''
    mkdir -p $out/include/harfbuzz $out/lib
    cp font.lib $out/lib/
    cp -r freetype-2.14.3/include/* $out/include/
    cp harfbuzz-14.5.1/src/hb*.h $out/include/harfbuzz/
  '';
}
