#!/bin/bash
# Runs extra-plants.txt with tools/planted.sh's loop (from the repo root).
d=$(mktemp -d); mkdir $d/plants; cp reviews/pr-53-repro/extra-plants.txt $d/plants/x.txt
sed "s#tests/plants/\*.txt#$d/plants/*.txt#" tools/planted.sh > tools/.planted-extra.sh
bash tools/.planted-extra.sh; rc=$?
rm -rf $d tools/.planted-extra.sh; exit $rc
