#!/bin/sh
# sh extra-plants.sh [FILE]: tools/planted.sh on one plant file only
# (default the package's own, tests/plants/infoview.txt), from the repo's top.
f=${1:-tests/plants/infoview.txt}
sed "s|cat tests/plants/\*.txt|cat $f|" tools/planted.sh > tools/planted-one.sh
bash tools/planted-one.sh; rc=$?
rm -f tools/planted-one.sh
exit $rc
