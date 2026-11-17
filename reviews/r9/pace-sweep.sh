#!/usr/bin/env bash
# Review of package 9: the sweep the PR describes but does not commit.
# For P 2..4, D 0..1 and every Gap 0..P+D+2 (39 bounds), visible, with
# HideCheck: TLC must pass NoWaste and Fresh exactly when Gap > P + D.
# usage: reviews/r9/pace-sweep.sh   (needs tlc on PATH)
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d); cp design/specs/Pace.tla "$tmp"/; cd "$tmp"; rc=0; n=0
for P in 2 3 4; do for D in 0 1; do for Gap in $(seq 0 $((P + D + 2))); do
  for prop in "INVARIANT NoWaste" "PROPERTY Fresh"; do
    printf 'CONSTANTS Keys = 4\n P = %s\n D = %s\n Gap = %s\n Visible = TRUE\n HideCheck = TRUE\nSPECIFICATION Spec\n%s\n' $P $D $Gap "$prop" > s.cfg
    out=$(tlc -workers 2 -deadlock -metadir "states/$P$D$Gap${prop: -1}" -config s.cfg Pace.tla 2>&1)
    if grep -q "No error has been found" <<<"$out"; then got=pass
    elif grep -qE "is violated|properties were violated" <<<"$out"; then got=fail; else got=error; fi
    states=$(grep -oE '[0-9,]+ distinct states found' <<<"$out" | tail -1)
    echo "P=$P D=$D Gap=$Gap ${prop#* }: $got ($states)"
  done
  n=$((n + 1))
done; done; done
echo "$n bounds"
rm -rf "$tmp"
