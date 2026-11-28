"""PR 179 review: every plant line applied and its file compiled with gcc
and clang under the build's flags (-O2 -c), so a plant that does not
compile is never counted as caught.  syn.py TREE PLANTFILE"""
import subprocess, sys, os
tree, pf = sys.argv[1], os.path.abspath(sys.argv[2]); os.chdir(tree)
line = [l for l in subprocess.run(["make","-n","compilers"],capture_output=True,text=True).stdout.splitlines() if l.startswith("gcc ")][0]
flags = line.split(" -O2 -o ")[0].split()[1:]
tline = [l for l in subprocess.run(["make","-n","test","TESTS=ZZZ"],capture_output=True,text=True).stdout.splitlines() if 'tests/test_$t.c' in l][0]
tflags = [w for w in tline.split(" -o build")[0].split()[1:] if w.startswith(("-I","-D","-std","-W"))]
bad = n = 0
for l in open(pf):
    l = l.rstrip("\n")
    if not l or l.startswith("#"): continue
    name, target, prep, file, old, new = l.split("~", 5)
    old, new = old.replace("¶","\n"), new.replace("¶","\n")
    src = open(file).read()
    if src.count(old) != 1: print("BROKEN", name); bad += 1; continue
    n += 1
    open(file,"w").write(src.replace(old,new))
    try:
        for cc in ("gcc","clang"):
            r = subprocess.run([cc]+(tflags if file.startswith("tests/") else flags)+["-O2","-c","-o","/dev/null",file],capture_output=True,text=True)
            if r.returncode: print("NO COMPILE", cc, name, r.stderr[:300]); bad += 1
    finally: open(file,"w").write(src)
print("%d plants, %d bad" % (n, bad))
