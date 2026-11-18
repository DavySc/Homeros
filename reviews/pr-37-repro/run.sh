#!/usr/bin/env bash
# Builds and runs the two harnesses from the repo root: reviews/pr-37-repro/run.sh
# keydiff: key() of main (origin/main), of the PR, and of the PR without "j == 3 &&".
set -e
cd "$(dirname "$0")/../.."
o=build/pr37; mkdir -p $o
v() { make -s --eval="pv:;@echo \$($1)" pv; }
SRCS=$(v SRCS); OBJS=$(v OBJS); CPP=$(v CPPFLAGS); LIBS=$(v LIBS)
REST=$(echo $SRCS | tr ' ' '\n' | grep -v '^src/tty/tty.c$' | tr '\n' ' ')
git show origin/main:src/tty/tty.c > $o/tty_main.c
sed 's/j == 3 && s\[2\] == /s[2] == /' src/tty/tty.c > $o/tty_noj.c
cmp -s src/tty/tty.c $o/tty_noj.c && echo "(this tree already has no 'j == 3 &&': the variant is the PR itself; run at ecaaee3 for the comparison)"
for w in main pr noj; do
	f=$o/tty_$w.c; [ $w = pr ] && f=src/tty/tty.c
	gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude $CPP -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	    -DTTY="\"$PWD/$f\"" -o $o/keydiff_$w reviews/pr-37-repro/keydiff.c $REST $OBJS $LIBS
	./$o/keydiff_$w > $o/keys_$w.txt
done
wc -l < $o/keys_pr.txt
paste -d' ' $o/keys_main.txt $o/keys_pr.txt | awk '$0 != "" && ($3 != $7 || $4 != $8) { d++; if (substr($1,1,8) != "1b5b337e" || $4 != -1 || $8 != 1114121 || $3 != 4 || $7 != 4) bad++ }
	substr($1,1,8) == "1b5b337e" { t++; if ($8 != 1114121) same++ }
	END { printf "main and PR differ on %d calls; %d of them are not ESC [ 3 ~ ... going from no key to KEY_DEL with length 4; %d calls start ESC [ 3 ~, %d of them do not give KEY_DEL\n", d, bad, t, same }'
echo "PR and PR without 'j == 3 &&' differ on $(diff $o/keys_pr.txt $o/keys_noj.txt | grep -c '^>') calls"
gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude $CPP -O1 -g -o $o/keys reviews/pr-37-repro/keys.c $SRCS $OBJS $LIBS
k() { printf '%-34s ' "$*"; ./$o/keys "$@"; }
for p in 0 10 200; do
	k $p 1b5b337e; k $p 1b 5b337e; k $p 1b5b 337e; k $p 1b5b33 7e; k $p 1b 5b 33 7e
	k $p 69 1b5b337e 1b; k $p 1b5b333b357e 78; k $p 1b5b33 1b5b337e
done
