# zig.zig built by each Zig release nixpkgs has (the releases syntax.c's
# zig_rel[] lists), each into its own static library whose C functions
# carry the release (zig17_parse), for one target: Linux (glibc 2.17 and
# up, as ghostty.nix) by default, x86_64-windows-gnu for the Windows build.
# A library only, so Zig adds no compiler-rt or UBSan runtime of its own
# (nm: its only undefined names are libc's memcpy, malloc and the like).
{ pkgs ? import (builtins.fetchTarball "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz") {},
  target ? "x86_64-linux-gnu.2.17" }:
let rel = { "15" = pkgs.zig_0_15; "16" = pkgs.zig_0_16; "17" = pkgs.zig_0_17; };
in pkgs.runCommandLocal "homeros-zig-${target}" { } ''
  mkdir -p $out
  ${pkgs.lib.concatStrings (pkgs.lib.mapAttrsToList (v: zig: ''
    mkdir -p $TMPDIR/${v} && cd $TMPDIR/${v}
    cp ${./zig.zig} zig.zig
    echo 'comptime { @import("zig.zig").exportAs("zig${v}_"); }' > root.zig
    ZIG_GLOBAL_CACHE_DIR=$TMPDIR/zc${v} HOME=$TMPDIR ${zig}/bin/zig build-lib -OReleaseFast -fPIC -lc \
      -target ${target} -mcpu=baseline root.zig -femit-bin=$out/libzig${v}.a
  '') rel)}
''
