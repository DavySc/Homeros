# the whole editor under Wine: one editor, later launches open frames in it
cd /home/claude/repro/e2e
H=/home/claude/wt156/build/wintrial/homeros.exe; L=/home/claude/repro/winlist.exe
t() { s=$(date +%s%N); wine $H "$@"; c=$?; echo "launch $*: exit $c after $(( ($(date +%s%N) - s) / 1000000 )) ms"; }
wine $H a.txt & sleep 8
wine $L
t b.txt; sleep 2; wine $L
t; sleep 2; wine $L
for i in 1 2 3 4 5 6 7 8; do wine $H f$i.txt & done; sleep 10; wine $L
t 'Z:\nonexistent-dir\x.txt'; sleep 2; wine $L
# :q! three times closes three frames (tab pages); count again
wine $L keys ':q!~'; sleep 1; wine $L keys ':q!~'; sleep 1; wine $L
import -window root /home/claude/repro/e2e.png 2>/dev/null
# quit all: remaining frames
for i in $(seq 14); do wine $L keys ':q!~'; sleep 0.5; done; sleep 2; wine $L
# after the editor has gone, a launch is the editor again
wine $H b.txt & sleep 6; wine $L; wine $L keys ':q!~'; sleep 2; wine $L
wineserver -k
