#!/usr/bin/env bash
# Reviewer's plants: the same 12 rules of Org broken in each build, to see
# which of the build's own checks notices: its recorded tests, then its
# fuzzer (30 s), then its comparison with Emacs (seed 1).
# xplants.sh REPO PLANTS "test TESTS=org" "fuzz FUZZERS=org FUZZ_SECONDS=30" "<oracle target>"
# A plant is: name ~ file ~ text ~ replacement
repo=$1; plants=$2; shift 2
while IFS='~' read -r name file old new; do
	[ -z "$name" ] && continue
	tmp=$(mktemp -d)
	(cd "$repo" && cp -r Makefile include ref src tests tools design "$tmp"/)
	if ! python3 - "$tmp/$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(p).read()
if s.count(old) != 1: sys.exit('plant does not apply (%d): %r' % (s.count(old), old))
open(p, 'w').write(s.replace(old, new))
PY
	then echo "BROKEN PLANT  $name"; rm -rf "$tmp"; continue; fi
	got="NOT CAUGHT"
	for target in "$@"; do
		if ! (cd "$tmp" && eval make $target) </dev/null >"$tmp.log" 2>&1; then
			why=$(grep -E 'CHECK\(|[1-9][0-9]* differ|WRONG|ERROR|deadly signal|FAIL' "$tmp.log" | grep -v -E '^(gcc|clang|cc) |^make' | head -1)
			if [ -n "$why" ]; then got="caught by make ${target%% *}: ${why:0:90}"; else got="BUILD BROKE in make ${target%% *}: $(grep -m1 -E 'error' "$tmp.log")"; fi
			break
		fi
	done
	echo "$name: $got"
	rm -rf "$tmp" "$tmp.log"
done < <(grep -v -e '^#' -e '^$' "$plants")
