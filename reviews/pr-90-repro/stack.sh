# stack.sh PROBE 2>/dev/null: the smallest text on which a search overflows the C stack (rc 139), by bisection
p=$1; t=$(mktemp)
try() { # $1 bytes of text, $2 kind
	if [ $2 = lines ]; then python3 -c "import sys; n=int(sys.argv[1]); open(sys.argv[2],'w').write(('select a, b from t where c = 1;\n'*(n//32+1))[:n]+'\nEND\n')" $1 $t; k='/select\_.\{-}END\r'
	else python3 -c "import sys; n=int(sys.argv[1]); open(sys.argv[2],'w').write(('ab'*n)[:n]+'\n')" $1 $t; k='/\(a\|b\)*c\r'; fi
	( XDG_STATE_HOME=$(mktemp -d) $p $t "$k" >/dev/null 2>&1 ) 2>/dev/null
}
for kind in lines oneline; do lo=100; hi=2000000
	while [ $((hi - lo)) -gt $((lo / 20)) ]; do mid=$(((lo + hi) / 2)); if try $mid $kind; then lo=$mid; else hi=$mid; fi; done
	echo "$kind: fine at $lo bytes, crashes at $hi (stack $(ulimit -s) KiB)"; done
rm -f $t
