#!/bin/bash
# orgplants.sh: the org-conform and fuzz plants of tests/plants/org.txt through tools/planted.sh, in a copy of the head
# that keeps only those plant lines (the "test TESTS=org" ones go through testplants.sh)
set -e
rm -rf /tmp/claude-0/-home-claude/7e8332f1-a164-53a9-899d-0949368b1bc8/scratchpad/r143/v2/pl; mkdir /tmp/claude-0/-home-claude/7e8332f1-a164-53a9-899d-0949368b1bc8/scratchpad/r143/v2/pl; cd /home/claude/Homeros
git archive af74df60 | tar -x -C /tmp/claude-0/-home-claude/7e8332f1-a164-53a9-899d-0949368b1bc8/scratchpad/r143/v2/pl
cd /tmp/claude-0/-home-claude/7e8332f1-a164-53a9-899d-0949368b1bc8/scratchpad/r143/v2/pl; cp tests/plants/org.txt /tmp/claude-0/-home-claude/7e8332f1-a164-53a9-899d-0949368b1bc8/scratchpad/r143/v2/org-all.txt
for f in tests/plants/*.txt; do [ $f = tests/plants/org.txt ] || : > $f; done
grep -v "~test TESTS=org~" /tmp/claude-0/-home-claude/7e8332f1-a164-53a9-899d-0949368b1bc8/scratchpad/r143/v2/org-all.txt > tests/plants/org.txt
export PATH=/home/claude/Homeros/tools-env/bin:$PATH
bash tools/planted.sh > /tmp/claude-0/-home-claude/7e8332f1-a164-53a9-899d-0949368b1bc8/scratchpad/r143/v2/out/orgplants.txt 2>&1 || true
echo done >> /tmp/claude-0/-home-claude/7e8332f1-a164-53a9-899d-0949368b1bc8/scratchpad/r143/v2/out/orgplants.txt
