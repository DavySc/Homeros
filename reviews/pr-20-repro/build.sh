#!/usr/bin/env bash
# Builds the review's C probes against the repo's sources (run from the repo
# root, after "make test TESTS=core_screens" for the Python scripts):
#   perf        cost of draw and edit on a million lines
#   marks_perf  cost of an edit after many undo groups, with and without a mark
#   alloc       allocations in 5000 frames after warm-up
#   stalejoin   an undo group's join flag outliving its buffer (exit 1 = bug)
set -e
here=reviews/pr-20-repro
pick() { ls src/$1/*.c 2>/dev/null || echo ref/$1.c; }
SRC="$(pick text) $(pick undo) $(pick uni) src/core/*.c $(pick keys) $(pick grid) $(pick file) $(pick syntax) $(pick theme)"
mkdir -p build/review
for p in perf marks_perf stalejoin; do gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude -O2 -o build/review/$p $here/$p.c $SRC; done
gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude -O2 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc -o build/review/alloc $here/alloc.c $SRC
echo "built build/review/{perf,marks_perf,alloc,stalejoin}"
