# builds each plant of FILE (C files only) with gcc and clang, the Makefile's flags, -fsyntax-only -Werror.  usage: syn.py TREE FILE
import subprocess, os, sys, re
tree, pf = sys.argv[1], os.path.abspath(sys.argv[2]); os.chdir(tree)
line = [l for l in subprocess.run("make -n test TESTS=ZZZ", shell=True, capture_output=True, text=True).stdout.splitlines() if "tests/test_$t.c" in l][0]
flags = re.sub(r" -o build.*", "", line).replace("gcc", "", 1).split()
flags = [f for f in flags if not f.startswith("-fsanitize") and not f.startswith("-fno-sanitize")]
n = bad = 0
for l in open(pf):
    l = l.rstrip("\n")
    if not l or l.startswith("#"): continue
    name, target, prep, file, old, new = l.split("~", 5)
    if not file.endswith(".c"): print("skipped (not C)", name); continue
    old, new = old.replace("¶", "\n"), new.replace("¶", "\n")
    src = open(file).read()
    if src.count(old) != 1: print("BROKEN PLANT ", name); bad += 1; continue
    n += 1
    open(file, "w").write(src.replace(old, new))
    try:
        for cc in ("gcc", "clang"):
            r = subprocess.run([cc] + flags + ["-Itests", "-Isrc/sql", "-fsyntax-only", file], capture_output=True, text=True)
            if r.returncode:
                bad += 1
                e = [x for x in r.stderr.splitlines() if "error" in x]
                print("NO BUILD %-5s" % cc, name, "|", (e[0] if e else r.stderr[-200:])[:160], flush=True)
    finally: open(file, "w").write(src)
print("%d plants built, %d failures" % (n, bad))
