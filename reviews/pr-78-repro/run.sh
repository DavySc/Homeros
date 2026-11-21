#!/bin/sh
# PR 78 review repros.  Run from a checkout of the PR merged with main, after
# `make test TESTS=snip`, with tools-env/bin on PATH.  CAT: Davy's catalogue
# (project upload afd7163a-..., saved as snippets/sql.md).
set -e
R=$(dirname "$0")
sed '$d' src/snip/conform/snip.py > "$R/snipmod.py"   # snip.py without its main() call
make test TESTS=snip >/dev/null
echo "A: Insert mode in a buffer with no name (UBSan stops it)"
bash "$R/cc.sh" "$R/noname.c" /tmp/noname && /tmp/noname || true
echo "B: cursor after a mirror is rewritten"
python3 -I "$R/prefix.py" build/san/test_snip '${2:a b}a${2:a b}' '' 'x <Right> <S-Tab> <BS>'
python3 -I "$R/prefix.py" build/san/test_snip '${1:ab}-${1:ab}--' '' 'x <Right> <Right> z z <BS>'
echo "C: an extra undo step"
python3 -I "$R/prefix.py" build/san/test_snip '${1:ab}-${1:ab}' 'x ' '<Esc> u u'
echo "K: text typed at a final stop on another line (second pass)"
printf '```sql\n-- k : x\n${1:a}\nx\n```\n' > /tmp/k.md
python3 -I "$R/batch.py" build/san/test_snip /tmp/k.md 'q <Tab> e <Esc> u'
[ -n "$CAT" ] && for k in '<Esc> u' 'q <Esc> u u' 'q <Tab> w <Esc> u u' 'q <Tab> <Tab> <Esc> u u <C-r> <C-r>' 'q <Tab> w <Tab> e <Esc> u u u <C-r>'; do python3 -I "$R/batch.py" build/san/test_snip "$CAT" "$k" | tail -1; done
