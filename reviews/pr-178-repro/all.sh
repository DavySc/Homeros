#!/bin/bash
# all.sh 'text with \n escapes': Org and ours at every character, each position in a fresh Emacs; prints the differing ones
printf "$1" > rv/q.org
n=$(wc -c < rv/q.org); r=0
ours=$(rv/drv_san -o rv/q.org <(true) 2>/dev/null)
python3 - <<PY
import subprocess
b=open('rv/q.org','rb').read()
bad=0
for p in range(len(b)+1):
    if p<len(b) and (b[p]&0xc0)==0x80: continue
    open('rv/q.org.cmd','w').write(str(p))
    a=subprocess.run(['emacs','-Q','--batch','-l','tools/org_open.el','rv/q.org'],capture_output=True,stdin=subprocess.DEVNULL).stdout.split(b'\n',1)[1]
    c=subprocess.run(['rv/drv_san','-o','rv/q.org'],capture_output=True).stdout.split(b'\n',1)[1]
    if a!=c: bad+=1; print('  ',a.strip().decode('utf-8','replace')[:90],'|',c.strip().decode('utf-8','replace')[:90])
print(repr(b.decode())[:100], '=>', bad, 'of', len(b)+1, 'differ')
PY
