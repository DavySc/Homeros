#!/bin/bash
# spin.sh <rounds>: the battery beside 8 busy loops on 4 cores
pids=; for i in 1 2 3 4 5 6 7 8; do (while :; do :; done) & pids="$pids $!"; done
/tmp/claude-0/-home-claude/7e8332f1-a164-53a9-899d-0949368b1bc8/scratchpad/r143/v2/battery.sh spin $1 > /tmp/claude-0/-home-claude/7e8332f1-a164-53a9-899d-0949368b1bc8/scratchpad/r143/v2/out/log-spin.txt 2>&1
kill $pids
