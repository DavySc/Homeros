#!/bin/bash
# VS Code's colours against lean.c's for whole files: cmp.sh FILE...
# (from the repo's root; NODE_PATH as tests/test_syntax_lean.js asks, lean on PATH)
D=reviews/pr-144-repro; mkdir -p build
# the oracle with its cases replaced by the files given
sed "s|const pal = \[\], out = \[\];|if (process.env.FILES) { cases.length = 0; for (const f of process.env.FILES.split(' ')) cases.push(fs.readFileSync(f,'utf8')); }\n  const pal = [], out = [];|" tests/test_syntax_lean.js > build/oracle-files.js
[ -x build/replay144 ] || gcc -O1 -g -fsanitize=address,undefined -std=c17 -D_DEFAULT_SOURCE -Iinclude -Isrc/syntax -Itests -o build/replay144 $D/replay.c src/syntax/tsql.c src/syntax/lean.c src/text/*.c src/uni/*.c ref/theme.c || exit 1
FILES="$*" node build/oracle-files.js record > build/cmp144.txt && build/replay144 -v build/cmp144.txt 2>&1 | grep -v CHECK
