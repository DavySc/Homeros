#!/bin/bash
# oracle_msf.sh <msfq binary> <file.pdb>...: every stream of every file, read by our reader, must
# be byte for byte what llvm-pdbutil exports.
set -u
msfq=$1; shift; total=0
for pdb in "$@"; do
  d=$(mktemp -d); mkdir "$d/ours"
  "$msfq" "$pdb" "$d/ours" > "$d/list" || { echo "oracle_msf: cannot read $pdb"; exit 1; }
  want=$(llvm-pdbutil dump --summary "$pdb" | sed -n 's/ *Number of streams: //p')
  [ "$(wc -l < "$d/list")" = "$want" ] || { echo "oracle_msf: $pdb has $want streams, reader says $(wc -l < "$d/list")"; exit 1; }
  while read -r i n; do
    : > "$d/theirs"
    [ "$n" = 0 ] || llvm-pdbutil export --stream="$i" --out="$d/theirs" "$pdb" >/dev/null 2>&1
    cmp -s "$d/ours/$i" "$d/theirs" || { echo "oracle_msf: $pdb stream $i differs"; exit 1; }
    total=$((total + 1))
  done < "$d/list"
  rm -rf "$d"
done
echo "oracle_msf: $total streams of $# files agree with llvm-pdbutil"
[ "$total" -ge 15 ]
