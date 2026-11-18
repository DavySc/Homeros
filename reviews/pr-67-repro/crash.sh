#!/usr/bin/env bash
# Finding A of reviews/pr-67.md: keys after a SQL format that shortens the buffer.
# usage: crash.sh DRIVER   (tests/conform_drv built with the sanitizers)
drv=$(realpath "$1"); d=$(mktemp -d); cd "$d"
k() { python3 -c "import sys,re;C={'<CR>':13,'<Esc>':27};print(' '.join(str(C.get(t) or ord(t)) for t in re.findall('<[^<>]+>|.',sys.argv[1])))" "$1"; }
for keys in 'G:Format<CR>' 'G:Format<CR>x' 'G:Format<CR>D' 'G:Format<CR>vd' '7GgqGx' '8GgqjD' 'G:w<CR>x'; do
	printf 'select 1\n\n\n\n\n\n\n\nSELECT   2,   3 FROM t\n' > b.sql
	[ "$keys" = 'G:w<CR>x' ] && touch sqldiff.toml
	echo "== $keys"; k "$keys" | "$drv" b.sql 2>&1 | grep -m1 -E 'cursor|ERROR'
done
rm -rf "$d"
