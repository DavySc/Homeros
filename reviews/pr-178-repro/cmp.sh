#!/bin/bash
# cmp.sh FILE: Org's and our answers at FILE.cmd's positions, side by side where they differ
f=$1
diff <(emacs -Q --batch -l tools/org_open.el $f 2>/dev/null </dev/null) <(rv/drv_san -o $f) | head -${2:-12}
