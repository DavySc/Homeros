#!/bin/sh
# run.sh A|B scenario   -> one bench line
S=$(dirname "$0"); . $S/env.sh
bin=/home/claude/wt$1/build/wl/bench
case $2 in
 type2000) f=$S/empty.txt; : > $f; n=2000; keys="wtype -d 1 -k Escape -k Escape i$(head -c 2000 /dev/zero | tr '\0' x)";;
 type200)  f=$S/empty.txt; : > $f; n=200; keys="wtype -d 10 -k Escape -k Escape i$(head -c 200 /dev/zero | tr '\0' x)";;
 scroll)   f=$S/big.txt; n=399; keys="wtype -d 10 -k Escape -k Escape $(head -c 400 /dev/zero | tr '\0' j)";;
 page)     f=$S/big.txt; n=149; keys="wtype -d 30 -k Escape -k Escape $(for i in $(seq 50); do printf " G 1 G"; done)";;
esac
$bin $f $n > $S/out.$$ & p=$!
sleep 0.7; $keys; wait $p; echo "$1 $2 $(cat $S/out.$$)"; rm -f $S/out.$$
