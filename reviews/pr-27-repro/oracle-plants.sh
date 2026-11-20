#!/usr/bin/env bash
# Runs map.c (the identity oracle) against src/lsp with each of the plants
# the package's own test missed that touch the mapping.  usage: oracle-plants.sh REPO
here=$(cd "$(dirname "$0")" && pwd); repo=${1:?repo}
plant() {   # name, old, new, map arguments
	tmp=$(mktemp -d); mkdir -p "$tmp/src"; cp -r "$repo/include" "$tmp/"; cp -r "$repo/src/lsp" "$repo/src/text" "$tmp/src/"
	python3 - "$tmp/src/lsp/lsp.c" "$2" "$3" <<'PY' || { echo "BROKEN PLANT $1"; return; }
import sys
p, old, new = sys.argv[1:4]
s = open(p).read()
if s.count(old) != 1: sys.exit(1)
open(p, 'w').write(s.replace(old, new))
PY
	sh "$here/build.sh" "$tmp" "$tmp/map" "$here/map.c" 2>/dev/null
	if ASAN_OPTIONS=detect_leaks=0 "$tmp/map" $4 >"$tmp/log" 2>&1; then echo "NOT CAUGHT  $1"; else echo "caught      $1: $(grep -m1 -E 'wrong|ERROR|runtime error' "$tmp/log")"; fi
	rm -rf "$tmp"
}
plant "a full log never drops its older half" "	if (d->nlog == LOG) {" "	if (0) {" "7 3 burst"
plant "a full log still maps from the old floor" "		d->floor = d->nlog ? d->log[0].v + 1 : d->ver + 1;" "" "7 3 burst"
plant "incremental diagnostics for a new version keep the old version's" "	if (!add || ver != v->ver) clear(v);" "	if (!add) clear(v);" "1 50"
