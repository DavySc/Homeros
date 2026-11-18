#!/usr/bin/env bash
# Review of PR 26: the fuzz_keys_all plant alone, N times, and fuzz_keys_all
# without a plant, N times (tools/planted.sh on a scratch copy holding only
# that plant).  usage: tilde-plant.sh N   (from the repo root)
n=${1:-3}
for i in $(seq $n); do make fuzz FUZZERS=keys_all 2>&1 | grep -E "^fuzz_keys_all|SUMMARY|FAIL" | tr '\n' ' '; echo; done
tmp=$(mktemp -d)
cp -r Makefile include ref src tests tools design "$tmp"/
grep "fuzz_keys_all" tests/plants/keys.txt > "$tmp/one.txt"
rm "$tmp"/tests/plants/*.txt; mv "$tmp/one.txt" "$tmp/tests/plants/keys.txt"
for i in $(seq $n); do bash "$tmp/tools/planted.sh" | cut -c1-200; done
rm -rf "$tmp"
