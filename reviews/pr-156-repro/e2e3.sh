cd /home/claude/repro/e2e
H=/home/claude/wt156/build/wintrial/homeros.exe; L=/home/claude/repro/winlist.exe
wine $H a.txt & sleep 8
for i in $(seq 36); do echo "n $i" > n$i.txt; wine $H n$i.txt; done; sleep 4; wine $L
wine $H b.txt; echo "launch past the limit: exit $?"; sleep 2; wine $L
wineserver -k
