#!/bin/bash
# Review of package 12: N random 10-line windows per language, our faces
# (build/dump FILE win) against the marks Neovim's highlighter sets when it
# really redraws that window (screen.lua).  Lines over 399 bytes and lines Neovim's window had no room for are skipped.
# usage: screen.sh N SEED   (from the repo root, after building build/dump)
n=$1; RANDOM=$2
VRT=$(nvim --headless --clean -l /dev/stdin <<<'io.write(vim.env.VIMRUNTIME)')
c=(ref/*.c tests/*.c src/syntax/*.c build/syntax/src/tree-sitter/lib/src/*.c)
lua=($VRT/lua/vim/*.lua $VRT/lua/vim/treesitter/*.lua $VRT/lua/vim/lsp/*.lua)
md=(design/*.md *.md reviews/*.md $VRT/../../../share/doc/*/*.md)
total=0; bad=0; lines=0; badlines=0
for lang in c lua md; do
  declare -n fs=$lang
  for i in $(seq 1 $n); do
    f=${fs[$((RANDOM % ${#fs[@]}))]}
    [ -f "$f" ] || continue
    nl=$(wc -l < "$f"); [ "$nl" -gt 0 ] || continue
    l0=$(( (RANDOM * 32768 + RANDOM) % nl ))
    ./build/dump "$f" win $l0 10 > build/d.out || { echo "dump failed $f $l0"; exit 1; }
    nvim --headless --clean -l "$(dirname "$0")/screen.lua" "$f" $l0 10 > build/s.out 2>build/s.err || { echo "nvim failed $f $l0: $(head -2 build/s.err)"; continue; }
    total=$((total + 1))
    d=$(paste -d'\t' build/d.out build/s.out | awk -F'\t' 'length($1) < 400 && $2 != "SKIP" { n++; if ($1 "" != $2 "") b++ } END { print n+0, b+0 }')
    lines=$((lines + ${d% *})); b=${d#* }
    if [ "$b" -gt 0 ]; then bad=$((bad + 1)); badlines=$((badlines + b)); echo "DIFF $f $l0: $b lines"; fi
  done
done
echo "screen: $bad of $total windows differ ($badlines of $lines lines)"
