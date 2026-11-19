# cmp.py DIR: Neovim's <i>.want against json.c's <i>.json.got
import sys
d=sys.argv[1]; names=open(d+'/names.txt').read().split('\n')
same=0
for i,k in enumerate(names):
    src=open('%s/%d.json'%(d,i),'rb').read(); w=open('%s/%d.want'%(d,i),'rb').read(); g=open('%s/%d.json.got'%(d,i),'rb').read()
    err = w[:1]==b'!'
    if err: w=w[1:]
    if w==g: same+=1; continue
    nd=sum(1 for a,b in zip(w,g) if a!=b)
    print('%-16s %s %d of %d bytes differ' % (k, 'syntax error' if err else 'valid       ', nd, len(src)))
    if len(src) < 140:
        print('   src ', src.decode('latin-1').replace('\n','~').replace('\r','~').replace('\t',' ')); print('   nvim', w.decode()); print('   ours', g.decode())
    else:
        j=next(i for i,(a,b) in enumerate(zip(w,g)) if a!=b); print('   first at', j, repr(src[max(0,j-20):j+30]), w[max(0,j-20):j+30], g[max(0,j-20):j+30])
print(same, 'of', len(names), 'same')
