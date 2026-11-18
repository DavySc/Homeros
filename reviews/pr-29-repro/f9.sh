# SPC h r on one changed line resets every change within 3 lines of each other
sh src/git/mkrepo.sh /tmp/rv/r1
(cd /tmp/rv/r1 && git checkout -q a.txt && sed -i 's/^two$/TWO/; s/^five$/FIVE/; s/^eight$/EIGHT/; s/^eleven$/ELEVEN/' a.txt)
reviews/pr-29-repro/signs /tmp/rv/r1/a.txt 1 r
git -C /tmp/rv/r1 diff --stat -- a.txt
