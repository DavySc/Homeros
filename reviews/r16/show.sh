#!/bin/bash
# show.sh FILE: the first lines where each build leaves Org
f=$1; t=$(mktemp -d)
emacs -Q --batch -l canon.el $f > $t/e 2>/dev/null; ./dump_a $f > $t/a; ./dump_b $f > $t/b
echo "--- A vs org"; diff $t/e $t/a | head -${2:-6}; echo "--- B vs org"; diff $t/e $t/b | head -${2:-6}
