#!/usr/bin/env bash
# Pace.tla over more bounds than its committed configs: P 2 to 4, D 0 to 1,
# every Gap from 0 to P + D + 2, with the timer.  Claims: NoWaste passes
# exactly when Gap > P + D; Fresh passes at every bound.  Prints mismatches
# and a count; exits 1 on any.  About 3 minutes; needs tlc on PATH.
cd "$(dirname "$0")"; tmp=$(mktemp -d); cp Pace.tla "$tmp"/; cd "$tmp"
n=0; bad=0
for P in 2 3 4; do for D in 0 1; do for G in $(seq 0 $((P + D + 2))); do
	for prop in "INVARIANT NoWaste" "PROPERTY Fresh"; do
		printf 'CONSTANTS Keys = 4\n P = %s\n D = %s\n Gap = %s\n Visible = TRUE\n HideCheck = TRUE\n Timer = TRUE\nSPECIFICATION Spec\n%s\n' \
		  "$P" "$D" "$G" "$prop" > s.cfg
		out=$(tlc -workers 4 -deadlock -metadir "st$n" -config s.cfg Pace.tla 2>&1)
		if grep -q "No error has been found" <<<"$out"; then got=pass
		elif grep -qE "is violated|were violated" <<<"$out"; then got=fail; else got=error; fi
		want=pass; [ "$prop" = "INVARIANT NoWaste" ] && [ "$G" -le $((P + D)) ] && want=fail
		n=$((n + 1))
		[ "$got" = "$want" ] || { bad=$((bad + 1)); echo "MISMATCH P=$P D=$D Gap=$G $prop: want $want got $got"; }
	done
done; done; done
rm -rf "$tmp"
echo "pace-sweep: $n runs, $bad mismatches"
[ "$bad" = 0 ]
