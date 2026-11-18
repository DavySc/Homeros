#!/usr/bin/env bash
# Review of PR 53: reruns the review's checks.  From the repo root, with the
# check's tools on PATH (CLAUDE.md, "The check").  About 15 minutes; the
# extra plants (extra-plants.sh) and Wine (wine.sh, after `make wincc`) are
# run apart.
set -u
R=reviews/pr-53-repro
mk() { make -s -f Makefile -f $R/drv.mk drv DRV=$R/$1.c OUT=$1 || exit 1; }
mk eol_drv; mk wiring
echo "== the named cases (finding 2 and 3 show here; [noeol] in the written message is package 4's)"
CASES=1 SHOW=0 python3 $R/eol_diff.py build/san/eol_drv 1 1
echo "== random files, opened and written, no Ctrl-Z"
for s in 2 3 4 5 6; do CTRLZ=0 KNOWN=1 SHOW=2 python3 $R/eol_diff.py build/san/eol_drv 400 $s; done
echo "== random files, keys, written, no Ctrl-Z"
for s in 11 12 13 14 15; do CTRLZ=0 KNOWN=1 SHOW=2 python3 $R/eol_diff.py build/san/eol_drv 400 $s edit; done
echo "== with Ctrl-Z (finding 3)"
for s in 21 22; do KNOWN=1 SHOW=0 python3 $R/eol_diff.py build/san/eol_drv 400 $s; KNOWN=1 SHOW=0 python3 $R/eol_diff.py build/san/eol_drv 400 $s edit; done
echo "== the first line's length (finding 2)"
bash $R/firstline.sh build/san/eol_drv 100 8190 8191 8192 65536 480000
echo "== the crash journal"
bash $R/crash.sh build/san/eol_drv
echo "== undo across :e, syntax faces"
bash $R/wiring.sh build/san/wiring
echo "== file_eol and file_text_read against their statement, 60 s"
clang -std=c17 -D_DEFAULT_SOURCE -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -Iinclude -Isrc/file \
  -o build/fuzz_eol_review $R/fuzz_eol.c src/file/eol.c src/text/text.c && ./build/fuzz_eol_review -max_total_time=60 -max_len=400 2>&1 | tail -1
