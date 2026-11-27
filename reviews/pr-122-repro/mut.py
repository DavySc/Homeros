import subprocess
W='/home/claude/r122/'
src=open(W+'src/fold/fold.c').read()
A="\t\t\treturn lvl + 1;   /* fold_info()'s fi_level, for foldtext's dashes */"
muts=[
("returns 1 (the old answer)","\t\t\treturn 1;"),
("level counted from 0","\t\t\treturn lvl ? lvl : 1;"),
("one too deep","\t\t\treturn lvl + 2;"),
("foldlevel() of the line, not of the closed fold","\t\t\treturn fold_level(f, line);"),
("level capped at 2","\t\t\treturn lvl < 2 ? lvl + 1 : 2;"),
("level 1 when closed by 'foldlevel'","\t\t\treturn use_level ? 1 : lvl + 1;"),
("level 1 for a one-line fold","\t\t\treturn fp->len > 1 ? lvl + 1 : 1;"),
("level 1 when the fold has no nested folds","\t\t\treturn fp->nested.n ? lvl + 1 : 1;"),
("level 1 when the line is not the fold's first","\t\t\treturn rel == fp->top ? lvl + 1 : 1;"),
("deepest fold starting at the line (fi_low_level mixup)","\t\t\treturn lvl + 1 + (rel == fp->top && lvl > 0);"),
]
for name,b in muts:
    assert src.count(A)==1
    open('m.c','w').write(src.replace(A,b))
    r=subprocess.run(['gcc','-g','-fsanitize=address,undefined','-I'+W+'include','-o','tm',W+'tests/test_fold.c','m.c'],capture_output=True,text=True)
    if r.returncode: print('NOBUILD',name,r.stderr[-300:]);continue
    r=subprocess.run(['./tm',W+'tests/test_fold.txt'],capture_output=True,text=True,timeout=300)
    print('caught' if r.returncode else 'MISSED',name,'|',r.stderr.strip().split('\n')[0][:110],flush=True)
