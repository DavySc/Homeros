#!/usr/bin/env bash
# PR 42's probe against syntax.c without the line that moves ra/rz in
# syntax_edit.  From the repo root, after make.  Expect 1 differing frame.
set -e
d=$(mktemp -d); mkdir -p $d/src $d/reviews/pr-42-repro
cp -r src/syntax $d/src/; cp reviews/pr-42-repro/diff.c $d/reviews/pr-42-repro/
python3 - $d/src/syntax/syntax.c <<'P'
import sys
p = sys.argv[1]; s = open(p).read()
o = '\ts->ra = moved(s->ra, p, d, i), s->rz = moved(s->rz, p, d, i);\n'
assert s.count(o) == 1; open(p, 'w').write(s.replace(o, ''))
P
syn=$PWD/build/syntax
gcc -O2 -std=c17 -D_DEFAULT_SOURCE -I$PWD/include -I$syn/src/tree-sitter/lib/include -I$syn/src/tree-sitter/lib/src \
  -o $d/norz $d/reviews/pr-42-repro/diff.c src/text/*.c $d/src/syntax/lmatch.c $d/src/syntax/release.c \
  $syn/tree-sitter.o $syn/queries.o $(find $syn/g -name '*.o')
$d/norz reviews/pr-42-repro/far.c 26 2240 12 | tail -1
