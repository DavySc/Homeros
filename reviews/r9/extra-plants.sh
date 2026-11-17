#!/usr/bin/env bash
# Runs reviews/r9/extra-plants.txt through tools/planted.sh in a scratch copy
# of the repo whose tests/plants/ holds only that file.
set -eu
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
cp -r Makefile include ref src tests tools design "$tmp"/
rm "$tmp"/tests/plants/*.txt
cp reviews/r9/extra-plants.txt "$tmp"/tests/plants/
bash "$tmp"/tools/planted.sh; rc=$?
rm -rf "$tmp"
exit $rc
