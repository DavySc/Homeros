#!/usr/bin/env bash
# Review of PR 12: bugs planted in the contract (include/uni.h) AND the code
# (src/uni/uni.c) alike, so Frama-C still proves code == contract.  Only the
# tests against Neovim and Unicode's file can see such a bug.  For each plant
# runs `make test TESTS="uni uni_nvim"`; for one the tests miss, also
# `make frama` and `make fuzz FUZZERS=uni` to show the whole check passes.
# usage (repo root, tools on PATH): reviews/pr-12-repro/spec-plants.sh
# run from the repo root
while IFS='~' read -r name old new; do
	[ -z "$name" ] && continue
	tmp=$(mktemp -d)
	cp -r Makefile include ref src tests tools design "$tmp"/
	python3 - "$tmp" "$old" "$new" <<'PY' || { echo "BROKEN PLANT  $name"; continue; }
import sys
t, old, new = sys.argv[1:]
for f in ('include/uni.h', 'src/uni/uni.c'):
    s = open(t + '/' + f).read()
    hs = [o for o in old.split('@@') if o in s]
    if len(hs) != 1: sys.exit('no single match in ' + f)
    i = old.split('@@').index(hs[0])
    open(t + '/' + f, 'w').write(s.replace(hs[0], new.split('@@')[i]))
PY
	if (cd "$tmp" && make test TESTS="uni uni_nvim") >"$tmp.log" 2>&1; then
		f=fails; (cd "$tmp" && make frama) >"$tmp.frama" 2>&1 && f="passes ($(grep -o 'Proved goals:.*' "$tmp.frama"))"
		z=fails; (cd "$tmp" && make fuzz FUZZERS=uni) >"$tmp.fuzz" 2>&1 && z=passes
		echo "NOT CAUGHT  $name   (tests pass; make frama $f; fuzz_uni $z)"
	else echo "caught      $name   $(grep -m1 -E 'CHECK\(|FAIL' "$tmp.log")"; fi
	rm -rf "$tmp" "$tmp.log" "$tmp.frama" "$tmp.fuzz"
done <<'EOF2'
lam + alef with hamza below (U+0625) does not join~c == 0x625 || ~
lam + alef with hamza above (U+0623) does not join~c == 0x623 || ~
lam + alef (U+0627) does not join~ || c == 0x627~
GB12/13: a third regional indicator joins the flag~l == 8 && t == 8 ? 0 : ~
GB8: T x T dropped~(l == 7 || l == 5) && t == 5~l == 7 && t == 5
GB6: L x LVT dropped~t == 6 || t == 7~t == 6
GB9b: Prepend does not join~|| l == 10 ||~||
GB9: ZWJ does not join~t == 2 || t == 11 || t == 9~t == 2 || t == 9
width: a regional indicator before VS16 stays narrow~gcb(cpx(s, n, i)) == 12 || gcb(cpx(s, n, i)) == 8@@g == 12 || g == 8~gcb(cpx(s, n, i)) == 12 || gcb(cpx(s, n, i)) == 99@@g == 12 || g == 99
EOF2
