#!/usr/bin/env bash
# The scanner's leak, outside the editor.  From the repo root after
# `make compilers` (needs build/syntax).  Usage: leak.sh FILE.sql...
# orig: the scanner as nvim-treesitter's revision has it (the patch's line
# taken back out, compared with the fetched source by the review);
# patched: build/syntax/src/sql/scanner.c, what the PR compiles.
S=build/syntax; R=reviews/pr-51-repro; mkdir -p build/rv
sed 's/== 0) { free(start_tag);$/== 0) {/' $S/src/sql/scanner.c > build/rv/scanner_orig.c
for v in orig patched; do
	src=$S/src/sql/scanner.c; [ $v = orig ] && src=build/rv/scanner_orig.c
	gcc -g -O1 -fsanitize=address,undefined -I$S/src/tree-sitter/lib/include -I$S/src/sql \
	  -o build/rv/leak_$v $R/leak.c $src $S/g/sql/parser.o $S/tree-sitter.o || exit 1
	for f in "$@"; do
		echo "== $v $f"; ./build/rv/leak_$v "$f" 2>&1 | grep -E "bytes, error|SUMMARY|leaked in" | sort | uniq -c
	done
done
