w=$1; d=$2
/home/claude/wt$w/homeros big.txt & p=$!; sleep 1; wtype -k Escape; sleep 0.3; wtype j; sleep 0.4; grim -t ppm ref-$w.ppm
wtype -d $d $(python3 -c "print('kj'*40)"); sleep 0.5; grim -t ppm fast-$w.ppm; kill $p; wait $p 2>/dev/null
python3 - $w $d <<'P'
import sys
w=sys.argv[1]
def rd(p):
    d=open(p,'rb').read(); h=d.split(b'\n',3); W,H=map(int,h[1].split()); return W,H,h[3]
W,H,x=rd(f"ref-{w}.ppm"); _,_,y=rd(f"fast-{w}.ppm")
rows=sorted({i//3//W for i in range(0,len(x),3) if x[i:i+3]!=y[i:i+3]})
print(w,"delay",sys.argv[2],"ms: after 40 x 'kj' the screen differs from the same state drawn slowly in",len(rows),"pixel rows", (rows[0],rows[-1]) if rows else "")
P
