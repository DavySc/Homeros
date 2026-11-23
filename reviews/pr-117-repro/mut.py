import subprocess,sys,os
W='/home/claude/w117/'
src=open(W+'src/fold/fold.c').read()
muts=[
("level: first-line rule uses the relative line","if (l->lvl < 0 && lnum <= 1) l->lvl = 0;","if (l->lvl < 0 && l->lnum <= 1) l->lvl = 0;"),
("level: first two lines count as first","lnum <= 1) l->lvl = 0;","lnum <= 2) l->lvl = 0;"),
("level: every -1 is 0","if (l->lvl < 0 && lnum <= 1) l->lvl = 0;","if (l->lvl < 0) l->lvl = 0;"),
("recurse: lnum stays at the defined line","\t\t\tl->lnum = ll;\n",""),
("recurse: ll from lnum","long ll = l->lnum_save + 1;","long ll = l->lnum + 1;"),
("recurse: level 0 skipped as undefined","} while (l->lvl < 0);","} while (l->lvl <= 0);"),
("recurse: lnum_save not moved","\t\t\tl->lnum_save = l->lnum;\n\t\t\tl->lnum = ll;","\t\t\tl->lnum = ll;"),
("recurse: !fp return gives lnum","if (!fp) return bot;","if (!fp) return l->lnum - 1;"),
("update: backup starts at top","Line l = { .lnum = top > 1 ? top - 1 : top, .n = n };","Line l = { .lnum = top, .n = n };"),
("update: start stays above top after backup","long start = l.lnum, stop = bot;","long start = top > 1 ? top - 1 : top, stop = bot;"),
("update: invalid sets only top","if (f->invalid) { top = 1; bot = n; f->invalid = 0; }","if (f->invalid) { top = 1; f->invalid = 0; }"),
("update: invalid sets only bot","if (f->invalid) { top = 1; bot = n; f->invalid = 0; }","if (f->invalid) { bot = n; f->invalid = 0; }"),
("update: invalid never cleared","if (f->invalid) { top = 1; bot = n; f->invalid = 0; }","if (f->invalid) { top = 1; bot = n; }"),
]
for name,a,b in muts:
    assert src.count(a)==1,(name,src.count(a))
    open('m.c','w').write(src.replace(a,b))
    r=subprocess.run(['gcc','-g','-fsanitize=address,undefined','-I'+W+'include','-o','tm',W+'tests/test_fold.c','m.c'],capture_output=True,text=True)
    if r.returncode: print('NOBUILD',name,r.stderr[-300:]);continue
    try:
        r=subprocess.run(['./tm',W+'tests/test_fold.txt'],capture_output=True,text=True,timeout=120); rc=r.returncode
    except subprocess.TimeoutExpired: rc='timeout'
    print('caught' if rc else 'MISSED',rc,name,flush=True)
