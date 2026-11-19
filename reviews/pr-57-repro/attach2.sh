#!/bin/bash
# As attach.sh, for attach2.c: each build run from a Wine cmd console with
# stderr to the console, to a file, and both streams to a file.
cd "$(dirname "$0")"
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/prefix
for v in pr base; do for s in console windows; do
	zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror $([ $v = base ] && echo -DBASE) -Wl,--subsystem,$s -o a2_${v}_$s.exe attach2.c || exit 2
	for how in "" "2> err.txt" "> err.txt 2>&1"; do
		rm -f err.txt
		script -qc "xvfb-run -a wine cmd /c 'a2_${v}_$s.exe $how'" /dev/null > t.txt 2>&1
		printf '%-5s %-8s %-16s file: %-28s console: %s\n' $v $s "[$how]" "'$(tr -d '\r\n' < err.txt 2>/dev/null)'" "'$(tr -d '\r\n\033' < t.txt | grep -o 'usage[^$]*FILE' | head -1)'"
	done
done; done
rm -f t.txt err.txt a2_*.exe a2_*.pdb
