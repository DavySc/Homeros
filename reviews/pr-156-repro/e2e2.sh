cd /home/claude/repro/e2e
H=/home/claude/wt156/build/wintrial/homeros.exe; L=/home/claude/repro/winlist.exe
wine $H a.txt & sleep 8
import -window root /home/claude/repro/e2e-1.png
wine $H b.txt; sleep 3; wine $L
import -window root /home/claude/repro/e2e-2.png
wine $L keys 'ihello'; sleep 1
import -window root /home/claude/repro/e2e-3.png
wineserver -k
