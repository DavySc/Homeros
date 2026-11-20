#!/usr/bin/env bash
# Runs the reviewer's plants (extra-plants.txt) with the repo's own
# planted.sh, in a scratch copy of the repo whose tests/plants/ holds only
# that file.  "NOT CAUGHT" lines are the finding: no check of the package
# looks at what the plant breaks.  usage: extra-plants.sh [repo root]
here=$(cd "$(dirname "$0")" && pwd); root=${1:-$(cd "$here/../.." && pwd)}
tmp=$(mktemp -d)
cp -r "$root"/{Makefile,include,ref,src,tests,tools,design} "$tmp"/
rm "$tmp"/tests/plants/*.txt
cp "$here/extra-plants.txt" "$tmp/tests/plants/"
bash "$tmp/tools/planted.sh"; rc=$?
rm -rf "$tmp"
exit $rc
