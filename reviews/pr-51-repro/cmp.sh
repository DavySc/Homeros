#!/usr/bin/env bash
# Our colours against Neovim's (tests/test_syntax.lua, no edits) for whole
# files: prints per file the bytes, the coloured bytes on each side, and the
# bytes whose face differs.  From the repo root after `make compilers`,
# tools-env/bin on PATH.  Usage: cmp.sh FILE.sql...   (files must end in \n)
mkdir -p build/rv
[ -x build/rv/faces ] || gcc -std=c17 -D_DEFAULT_SOURCE -g -O1 -fsanitize=address,undefined -Iinclude -o build/rv/faces \
  reviews/pr-51-repro/faces.c $(ls src/text/*.c src/syntax/*.c) build/syntax/tree-sitter.o \
  $(find build/syntax/g -name '*.o') build/syntax/queries.o -Ibuild/syntax/src/tree-sitter/lib/include -Ibuild/syntax/src/tree-sitter/lib/src || exit 1
for f in "$@"; do
	d=$(mktemp -d); cp "$f" $d/0.sql; : > $d/0.sql.ops
	./build/rv/faces $d/0.sql > $d/ours
	nvim --headless --clean -l tests/test_syntax.lua $d/out $d 0.sql
	python3 - $d "$f" <<'PY'
import sys
d, f = sys.argv[1], sys.argv[2]
a, b = open(d + '/ours', 'rb').read(), open(d + '/out', 'rb').read()
col = lambda s: sum(1 for c in s if c not in (10, 48))
diff = sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b))
print('%s: %d bytes, coloured %d here, %d in Neovim, %d differ, syntax error %s' % (f, len(a), col(a), col(b), diff, open(d + '/out.err').read()))
if diff:
    src = open(d + '/0.sql', 'rb').read().split(b'\n'); la = a.split(b'\n'); lb = b.split(b'\n')
    for i, (x, y) in enumerate(zip(la, lb)):
        if x != y:
            print('  line %d: %r\n    here   %s\n    Neovim %s' % (i + 1, src[i][:150], x[:150].decode('latin1'), y[:150].decode('latin1'))); break
PY
	rm -rf $d
done
