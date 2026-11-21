# libghostty-vt 1.0.1 (the vt-1.0.1 tag), the terminal's VT engine, built as
# its PACKAGING-LIBGHOSTTY.md says: from the Git checkout with -Demit-lib-vt,
# the Zig packages its deps/libghostty-vt.txt lists given with --system (the
# names are their Zig hashes, which `zig fetch` gave for these exact
# copies), for one target: Linux (glibc 2.17 and up, so any distribution's
# compiler links it) by default, x86_64-windows-gnu for the Windows build.  The packages come from GitHub, or from
# cache.nixos.org where nixpkgs' turbo-unwrapped already fetched them,
# because the cloud sessions cannot reach ghostty.org or codeberg.org.
{ pkgs ? import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {},
  target ? "x86_64-linux-gnu.2.17" }:
let
  git = url: rev: builtins.fetchGit { inherit url rev; shallow = true; };
  zip = name: url: hash: pkgs.fetchzip { inherit name url hash; };   # turbo-unwrapped's names, so the cache's copies match
  deps = {
    "aro-0.0.0-JSD1QtuBNwCASyBtNF3pqTl_W3oAJQGEVyFAtrBSE_Pa" = git "https://github.com/Vexu/arocc" "d0c8c4d9c55daa7ef6e40cf0f630a5b5e900989b";
    "N-V-__8AAGmZhABbsPJLfbqrh6JTHsXhY6qCaLAQyx25e0XE" = zip "highway" "https://deps.files.ghostty.org/highway-66486a10623fa0d72fe91260f96c892e41aceb06.tar.gz" "sha256-1xn+WAxeQyrz9KA3y4EOCr5Z94mom7J5TNdippPe2Sw=";
    "translate_c-2.0.0-Q_BUWltOBwBYABF84EUL208pIFeUIwbBs29FmVBaafN5" = zip "translate_c" "https://codeberg.org/ziglang/translate-c/archive/02ff0c523fb92a38939b04d0145c4a9602a74d56.tar.gz" "sha256-13iPyWXUhBQXhF7AKbzKwrVdIrDQrMBNL8OfMyFdde0=";
    "uucode-0.2.0-ZZjBPm6FVgBcY-79AHV8ckQj86z0JwOyVyj89VeZrEiv" = git "https://github.com/jacobsandlund/uucode" "1fb73433bba5d93366c57f23ff2e9d7939746500";
    "N-V-__8AAP5JWgCGP_AD0teWpa4krRvE9VPZzvviGdbmN4jI" = zip "wuffs" "https://deps.files.ghostty.org/wuffs-7411f488fe2e2c205c3d3b3d28638b7356522930.tar.gz" "sha256-sOuwt3R6xG7oqXaA66QGUjFHrEjGnTXjwYs6rSRb+7I=";
  };
in pkgs.stdenvNoCC.mkDerivation {
  name = "libghostty-vt-1.0.1-${target}";
  src = git "https://github.com/ghostty-org/ghostty" "108cacb17ed8990840cd14abfce43d8ed8503ca1";
  nativeBuildInputs = [ pkgs.zig pkgs.llvmPackages.llvm ];
  dontConfigure = true;
  dontInstall = true;
  dontFixup = true;
  buildPhase = ''
    mkdir $TMPDIR/p
    ${pkgs.lib.concatStrings (pkgs.lib.mapAttrsToList (k: v: "cp -r ${v} $TMPDIR/p/${k}\n") deps)}
    chmod -R u+w $TMPDIR/p
    export ZIG_GLOBAL_CACHE_DIR=$TMPDIR/zc HOME=$TMPDIR
    zig build -Demit-lib-vt --system $TMPDIR/p -Doptimize=ReleaseFast -Dcpu=baseline -Dtarget=${target} --prefix $out
    # The static library bundles Zig's compiler-rt and UBSan runtime, whose
    # memcpy, libm and __ubsan_* handlers then replaced glibc's and the
    # sanitizers' in every binary that links it (a planted overflow in
    # wl.c's memcpy passed ASan, on CI and here, 1 run each).  Made local to
    # the library, as Ghostty's own libsystem_override.sh does on macOS.
    # llvm-objcopy: GNU objcopy cannot write members named with a path.
    for a in $out/lib/*.a; do   # COFF has no local symbols to make; Windows runs no ASan
      [ -e "$a" ] || continue
      llvm-objcopy --wildcard --localize-symbol='__ubsan_*' $(llvm-nm -g --defined-only "$a" \
        | awk '/:$/ { m = /compiler_rt\.o:$/ } m && NF == 3 && $3 !~ /^__/ { print "--localize-symbol=" $3 }') "$a"
    done
  '';
}
