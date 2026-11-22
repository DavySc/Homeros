# usage: mut.sh NAME 'old' 'new': plants one change in src/core/draw.c, runs the trace, restores
cd /home/claude/Homeros
python3 - "$2" "$3" <<'PY'
import sys
p='src/core/draw.c'; s=open(p).read(); a,b=sys.argv[1],sys.argv[2]
assert s.count(a)==1, 'matches: %d' % s.count(a)
open(p,'w').write(s.replace(a,b))
PY
[ $? = 0 ] || { echo "$1: NOT APPLIED"; git checkout -q src/core/draw.c; exit; }
t=core_markdown
if gcc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -I/usr/include/freetype2 -I/usr/include/libpng16 -I/usr/include/harfbuzz -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include  -Ibuild/syntax/src/tree-sitter/lib/include -Ibuild/syntax/src/tree-sitter/lib/src -DHOMEROS_WL -Ibuild/wl  -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -o build/san/test_core_markdown tests/test_core_markdown.c src/text/text.c src/undo/undo.c src/uni/uni.c src/core/core.c src/core/draw.c src/core/move.c src/core/win.c src/keys/buf.c src/keys/edit.c src/keys/keys.c src/keys/motion.c src/keys/normal.c src/keys/ops.c src/grid/grid.c src/tty/tty.c src/loop/loop.c src/loop/timer.c src/loop/win.c src/file/eol.c src/file/file.c src/file/journal.c src/file/state.c src/file/win.c src/syntax/lmatch.c src/syntax/release.c src/syntax/syntax.c src/syntax/tsql.c ref/theme.c src/tools/dash.c src/tools/icons.c src/tools/match.c src/tools/oil.c src/tools/pick.c src/git/git.c src/git/keys.c src/git/os.c src/git/sec.c src/git/status.c src/git/win.c src/git/write.c ref/lsp.c ref/infoview.c src/font/font.c src/org/org.c src/shada/shada.c src/sqlfmt/sqlfmt.c src/wl/wl.c build/syntax/tree-sitter.o build/syntax/g/c/src/parser.o build/syntax/g/lua/src/parser.o build/syntax/g/lua/src/scanner.o build/syntax/g/markdown/tree-sitter-markdown/src/parser.o build/syntax/g/markdown/tree-sitter-markdown/src/scanner.o build/syntax/g/markdown/tree-sitter-markdown-inline/src/parser.o build/syntax/g/markdown/tree-sitter-markdown-inline/src/scanner.o build/syntax/g/rust/parser.o build/syntax/g/rust/scanner.o build/syntax/queries.o build/wl/protocols.o -lharfbuzz -lfontconfig -lfreetype  -lm -lwayland-client -lxkbcommon  2>/tmp/mut.err; then ./build/san/test_core_markdown > /tmp/mut.out 2>&1; echo "$1: $([ $? = 0 ] && echo MISSED || echo caught) $(tail -1 /tmp/mut.out)"; else echo "$1: does not compile"; fi
git checkout -q src/core/draw.c
