import subprocess
W='/home/claude/r122/'
src=open(W+'src/fold/fold.c').read()
R="return n ? 1 : done ? 0 : -1;"
muts=[
("found a fold, changed nothing: says it changed one",R,"return n || done ? 1 : -1;"),
("no fold found: says 0, no E490",R,"return n ? 1 : 0;"),
("fold found but nothing to do: E490",R,"return n ? 1 : -1;"),
("every round changed a fold: says 0","\t\treturn count > 0;","\t\treturn 0;"),
("zO does not open (opening only for zo)","op != FOLD_CLOSE, op == FOLD_OPEN_REC);","op == FOLD_OPEN, op == FOLD_OPEN_REC);"),
("zo opens nested folds too","op != FOLD_CLOSE, op == FOLD_OPEN_REC);","op != FOLD_CLOSE, op != FOLD_CLOSE);"),
("zc closes recursively arg","op != FOLD_CLOSE, op == FOLD_OPEN_REC);","op != FOLD_CLOSE, op != FOLD_OPEN);"),
("zv stops after one fold","\t\twhile (manual(f, lnum, 1, 0) & DONE_ACTION) done = 1;","\t\tif (manual(f, lnum, 1, 0) & DONE_ACTION) done = 1;"),
("zv says 1 always","\t\treturn done;\n","\t\treturn 1;\n"),
("zO runs once whatever the count (old shape) and says 1 always","\tcase FOLD_OPEN_REC:\n","\tcase FOLD_OPEN_REC: if (op == FOLD_OPEN_REC) return manual(f, lnum, 1, 1) ? 1 : -1;\n"),
]
for name,a,b in muts:
    assert src.count(a)==1,(name,src.count(a))
    open('m.c','w').write(src.replace(a,b))
    r=subprocess.run(['gcc','-g','-O1','-fsanitize=address,undefined','-I'+W+'include','-I'+W+'tests','-o','tm',W+'tests/test_fold.c','m.c'],capture_output=True,text=True)
    if r.returncode: print('NOBUILD',name,r.stderr[-300:]);continue
    r=subprocess.run(['./tm',W+'tests/test_fold.txt'],capture_output=True,text=True,timeout=300)
    if r.returncode: print('caught by trace:',name,'|',r.stderr.strip().split('\n')[0][:90],flush=True);continue
    r=subprocess.run(['clang','-g','-O1','-fsanitize=fuzzer,address,undefined','-fno-sanitize-recover=all','-I'+W+'include','-I'+W+'tests','-o','fm',W+'tests/fuzz_fold.c','m.c'],capture_output=True,text=True)
    if r.returncode: print('NOBUILD fuzz',name,r.stderr[-300:]);continue
    r=subprocess.run(['./fm','-max_total_time=30','-max_len=256','-artifact_prefix=/home/claude/p122/art-'],capture_output=True,text=True)
    print('caught by fuzz: ' if r.returncode else 'MISSED:',name,flush=True)
