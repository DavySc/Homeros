#!/usr/bin/env bash
# Runs the reviewer's plants with the repo's own planted.sh, in a scratch copy
# of the repo whose tests/plants/ holds only extra-plants.txt (SHARD passes on).
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd)
tmp=$(mktemp -d)
cp -r "$root"/{Makefile,assets,include,ref,src,tests,tools,design} "$tmp"/
rm "$tmp"/tests/plants/*.txt
cp "$here/extra-plants.txt" "$tmp/tests/plants/"
bash "$tmp/tools/planted.sh"; rc=$?
rm -rf "$tmp"
exit $rc
