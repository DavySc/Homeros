#!/bin/bash
# Builds attach.c as a console program and as a GUI-subsystem program, with and
# without the PR's line, and runs each from a Wine console (cmd) with stderr
# redirected to a file, as a script or a test that captures the message does.
cd "$(dirname "$0")"
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/prefix
for v in pr base; do for s in console windows; do
	zig cc -target x86_64-windows-gnu -std=c17 -Wall -Wextra -Werror $([ $v = base ] && echo -DBASE) -Wl,--subsystem,$s -o a_${v}_$s.exe attach.c || exit 2
	rm -f err.txt
	# script -qc gives wine a real terminal, so cmd has a console
	script -qc "xvfb-run -a wine cmd /c 'a_${v}_$s.exe 2> err.txt'" /dev/null > tty_${v}_$s.txt 2>&1
	printf '%-5s %-8s redirected stderr file: %-28s console: %s\n' $v $s "'$(tr -d '\r\n' < err.txt 2>/dev/null)'" "'$(tr -d '\r\n\033' < tty_${v}_$s.txt | grep -o 'usage[^$]*FILE' | head -1)'"
done; done
