#!/usr/bin/env bash
# Review of PR 52: runs tools/planted.sh on the reviewer's plants only
# (own-plants.txt), in a scratch copy of the repo.
set -e
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
git archive HEAD | tar -x -C "$tmp"
rm "$tmp"/tests/plants/*.txt
cp "${1:-reviews/pr-52-repro/own-plants.txt}" "$tmp/tests/plants/own.txt"
(cd "$tmp" && bash tools/planted.sh) || true
rm -rf "$tmp"
