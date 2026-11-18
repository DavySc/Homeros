#!/usr/bin/env bash
# Each half of the conform.py fix taken out alone, against the PR's plant
# (pr-plant.txt) and the two plants that replace it (PLANTS, default the
# conform lines of tests/plants/harness.txt).  Run from the repo root with
# tools-env/bin on PATH.  A third argument-free case hides the libraries
# (PKG_CONFIG_LIBDIR) to show what planted.sh says when the build fails.
d=$(dirname "$0"); R=$PWD
LIVE="s/elif 'error' not in g and known_rule(known, c):/elif known_rule(known, c):/"
EX="s/ or same(g, w) or 'error' in g:/ or same(g, w):/"
grep -e 'fails on a live' -e 'example makes' tests/plants/harness.txt > /tmp/two.$$
for p in "$d/pr-plant.txt" /tmp/two.$$; do
	for m in "" "$LIVE" "$EX"; do echo "### $(basename $p)  mutation: ${m:-none}"; bash "$d/run-plants.sh" "$R" "$p" "$m"; done
done
mkdir -p /tmp/nopc.$$; echo "### libraries hidden"; PKG_CONFIG_LIBDIR=/tmp/nopc.$$ bash "$d/run-plants.sh" "$R" /tmp/two.$$
rm -rf /tmp/two.$$ /tmp/nopc.$$
