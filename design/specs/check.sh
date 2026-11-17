#!/usr/bin/env bash
# Runs every model.  Each .cfg says on its first line whether TLC must
# pass or fail it ("\* expect: pass|fail").  A fail only counts if TLC
# reports a property violation.
# Needs tlc on PATH (nixpkgs#tlaplus).
# TLC writes a states/ directory; run from a local copy if this folder is slow.
cd "$(dirname "$0")"; rc=0
for cfg in *.cfg; do
  spec=${cfg%%_*}.tla
  # one state directory per config: TLC names it by the second, so two fast
  # models in the same second collide ("directory already exists")
  out=$(tlc -workers 4 -deadlock -metadir "states/${cfg%.cfg}" -config "$cfg" "$spec" 2>&1)
  # "fail" only counts if TLC found a property violation (not a parse error)
  if grep -q "No error has been found" <<<"$out"; then got=pass
  elif grep -qE "is violated|properties were violated" <<<"$out"; then got=fail
  else got=error; fi
  want=$(sed -n 's/^\\\* expect: //p' "$cfg")
  [ "$got" = "$want" ] && s=OK || { s=WRONG; rc=1; }
  printf '%-24s want %-4s got %-4s %s\n' "$cfg" "$want" "$got" "$s"
done
exit $rc
