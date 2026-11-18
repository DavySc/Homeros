#!/usr/bin/env bash
# usage: run-plants.sh REPO PLANTS.txt [sed-expression applied to tools/conform.py first]
# Runs the plants of PLANTS.txt through tools/planted.sh in a scratch copy of
# REPO, optionally with tools/conform.py mutated, so each half of the fix can
# be taken out alone.  Needs tools-env/bin on PATH (nvim).
set -e
t=$(mktemp -d); cp -r "$1"/{Makefile,include,ref,src,tests,tools,design} "$t"/
rm "$t"/tests/plants/*.txt; cp "$2" "$t"/tests/plants/only.txt
if [ -n "$3" ]; then sed -i "$3" "$t"/tools/conform.py; (cd "$t" && diff -q tools/conform.py "$1"/tools/conform.py >/dev/null && { echo "mutation did not apply"; exit 1; } || true); fi
bash "$t"/tools/planted.sh; echo "planted.sh exit $?"
rm -rf "$t"
