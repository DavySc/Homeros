#!/usr/bin/env bash
# Runs extra-plants.txt through tools/planted.sh.  From the repo root, tools-env/bin on PATH.
sed 's#cat tests/plants/\*.txt#cat reviews/pr-51-repro/extra-plants.txt#' tools/planted.sh > tools/planted-extra.sh
bash tools/planted-extra.sh; rm tools/planted-extra.sh
