# Sourced by the Windows checks (src/*/check.sh, src/undo/win.sh).
# pass SECONDS EXE [RUNNER...]: runs EXE (under RUNNER, e.g. wine) and
# passes only when it exits 0 AND its last line is its own summary
# ("test_x: ...").  Wine has reported status 0 for a test that trapped
# (package 17 measured 1 of 6 runs of an alignment plant, and PR 95's CI
# missed that plant): the summary is printed only after every check, and
# a line printed before it does not start with the test's name and colon.
pass() {
	local s=$1 exe=$2
	shift 2
	# a file, not a pipe: a child a failed test leaves alive would hold a
	# pipe open, and the check would wait on it (PR 96's review measured
	# 104 min on the plant "win: journal without the lock")
	local r=0
	timeout "$s" "$@" "$exe" > "${exe%.exe}.out" 2>&1 || r=$?
	cat "${exe%.exe}.out"
	[ $r = 0 ] && [ "$(tail -n 1 "${exe%.exe}.out" | cut -d: -f1)" = "$(basename "$exe" .exe)" ]
}

# winlink EXE TEST.c: builds EXE for Windows from TEST.c and every module
# wincc compiles (not main.c, the window or its drawing: src/paint, src/font), with tree-sitter, the
# grammars and queries outside our warnings (as src/syntax/build.mk), stb
# and libghostty-vt (build.mk): for a test that links the whole editor
# below the window (src/tools/check.sh, src/lsp/check.sh)
winlink() {
	local syn=build/syntax o objs= g
	local inc="-I$syn/src/tree-sitter/lib/include -I$syn/src/tree-sitter/lib/src"
	mkdir -p build/win/link
	local zig=$(make -s --eval 'zigwin: ; @echo $(ZIG_WIN)' zigwin)   # Zig's parsers (src/syntax/build.mk); thread 0's, agreed
	make -s $syn/queries.c build/win/stb.o build/ghostty/windows.lib $zig   # stb: src/image/build.mk; thread 0's, agreed
	# wincc's list itself, whatever is built yet
	local srcs=$(make -s --eval 'winsrcs: ; @echo $(WIN_SRCS)' winsrcs | tr ' ' '\n' | grep -v -e '^src/main\.c$' -e '^src/win/' -e '^src/paint/' -e '^src/font/')
	for g in $(sed -n '/^SYN_GRAMMARS/,/[^\\]$/p' src/syntax/build.mk | tr -d '\\' | cut -d= -f2); do
		o=build/win/link/$(echo $g | tr / _).o
		[ -f $o ] || zig cc -target x86_64-windows-gnu -O2 -c -I$(dirname $syn/src/$g) $syn/src/$g.c -o $o
		objs="$objs $o"
	done
	zig cc -target x86_64-windows-gnu -O2 -c $syn/queries.c -o build/win/link/queries.o
	[ -f build/win/link/ts.o ] || zig cc -target x86_64-windows-gnu -O2 -c $inc $syn/src/tree-sitter/lib/src/lib.c -o build/win/link/ts.o
	. src/tools/ghostty-win.sh; . src/sql/openssl-win.sh   # sql: package 22's, as thread 0 asked
	zig cc -target x86_64-windows-gnu -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -I/usr/include/stb $inc -O1 -g \
	  -fsanitize=undefined -fsanitize-trap=undefined -o "$1" "$2" $srcs build/win/link/queries.o build/win/link/ts.o $objs build/win/stb.o $GHOSTTY_WIN $SQL_WIN $zig
}
