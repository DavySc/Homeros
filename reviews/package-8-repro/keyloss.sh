#!/bin/sh
# keyloss.sh A|B delay_ms sleep_before  -> ok / which char missing
S=$(dirname "$0"); . $S/env.sh
t=$(python3 -c "print(''.join('abcdefghijklmnopqrstuvwxyz'[i%26]+str(i%10) for i in range(150)))")
f=$S/kl.$$.txt; rm -f $f
timeout 20 /home/claude/wt$1/homeros $f & p=$!
sleep $3
wtype -d $2 -k Escape -k Escape "i$t" -k Escape ":w" -k Return ":q" -k Return
sleep 0.5
if kill -0 $p 2>/dev/null; then st=hung; kill $p; else st=quit; fi
python3 - "$f" "$t" "$1" $st <<'P'
import sys,os
f,t,w,st=sys.argv[1:]
g=open(f).read().rstrip("\n") if os.path.exists(f) else None
if g==t: print(w,st,"ok")
elif g is None: print(w,st,"no file")
else:
    i=next((i for i,(a,b) in enumerate(zip(g,t)) if a!=b),min(len(g),len(t)))
    print(w,st,"differs: len",len(g),"want",len(t),"first diff at",i,repr(g[max(0,i-3):i+3]),repr(t[max(0,i-3):i+3]))
P
rm -f $f
