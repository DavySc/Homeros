#!/usr/bin/env bash
# Runs the reviewer's plants with the repo's own planted.sh, in a scratch copy
# of the repo whose tests/plants/ holds only the reviewer's file:
# extra-plants.txt (first review, head a240489), or with argument 2
# extra-plants-2.txt (re-review, head 2204e10).
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd)
tmp=$(mktemp -d)
cp -r "$root"/{Makefile,include,ref,src,tests,tools,design} "$tmp"/
rm "$tmp"/tests/plants/*.txt
cp "$here/extra-plants${1:+-$1}.txt" "$tmp/tests/plants/"
bash "$tmp/tools/planted.sh"; rc=$?
rm -rf "$tmp"
exit $rc
