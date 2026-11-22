#!/usr/bin/env bash
# Feeds tools/winpass.sh's pass fake tests: sh -c SCRIPT stands in for wine.
# usage: bash fool.sh /path/to/repo
. "$1/tools/winpass.sh"
t() { # name, expected (pass|fail), script
	if pass 5 test_x.exe sh -c "$3" sh >/dev/null; then r=pass; else r=fail; fi
	printf '%-58s want %s got %s%s\n' "$1" "$2" "$r" "$([ $r = $2 ] || echo '   <-- FOOLED')"
}
t "summary last, status 0"                         pass 'echo "test_x: all good"'
t "summary last, status 1"                         fail 'echo "test_x: all good"; exit 1'
t "no output, status 0 (trap, status lost)"        fail ':'
t "measured line then cut off, status 0"           fail 'echo "test_x (measured): 3 ms"'
t "CHECK failure line, status 0"                   fail 'echo "tests/test_x.c:12: CHECK(a) failed"'
t "another test's summary, status 0"               fail 'echo "test_xy: all good"'
t "summary, then a later line, status 0"           fail 'echo "test_x: all good"; echo "X connection to :99 broken"'
t "summary without a newline, status 0"            pass 'printf "test_x: all good"'
t "summary with CR LF, status 0"                   pass 'printf "test_x: all good\r\n"'
t "hang past the time limit after the summary"     fail 'echo "test_x: all good"; sleep 20'
t "early own-name line then cut off, status 0"     fail 'echo "test_x: phase 1 took 3 ms"'
t "test_win handler line (test_x: FAIL, ...), status 0" fail 'echo "test_x: FAIL, exception 0xc000001d"'
