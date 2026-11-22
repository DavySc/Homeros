"""PR 93 review: extra plants in org_todo_step's code (mutants.txt:
name~old~new, ¶ a newline), each run against the unit test (as the PR's
TODO plants are) and against Org's answers on text sets.
mutants.py REPO MUTANTS DIR..."""
import os, subprocess, sys, tempfile
repo, mf, dirs = sys.argv[1], sys.argv[2], sys.argv[3:]
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(repo, 'src/org/org.c')).read()
cc = ['gcc', '-std=c17', '-D_DEFAULT_SOURCE', '-I' + repo + '/include', '-I' + repo + '/tests', '-I' + repo + '/src/org', '-O1', '-g',
      '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
for line in open(mf).read().split('\n'):
    if not line: continue
    name, old, new = line.split('~')
    old, new = old.replace('¶', '\n'), new.replace('¶', '\n')
    if src.count(old) != 1: print('NOT UNIQUE (%d): %s' % (src.count(old), name)); continue
    t = tempfile.mkdtemp()
    open(t + '/org.c', 'w').write(src.replace(old, new))
    res = []
    for exe, main in (('test_org', 'tests/test_org.c'), ('drv', 'tests/org_drv.c')):
        b = subprocess.run(cc + ['-o', t + '/' + exe, repo + '/' + main, t + '/org.c', repo + '/src/text/text.c', repo + '/src/uni/uni.c'], capture_output=True)
        if b.returncode: res.append('does not build'); break
    else:
        u = subprocess.run([t + '/test_org'], capture_output=True)
        res.append('unit test: ' + ('caught' if u.returncode else 'SURVIVES'))
        for d in dirs:
            c = subprocess.run(['python3', here + '/cmp.py', t + '/drv', d], capture_output=True, cwd=repo)
            last = c.stdout.decode().strip().split('\n')[-1]
            res.append('%s: %s' % (os.path.basename(d), 'caught (' + last + ')' if not last.startswith('0 of') else 'SURVIVES'))
    print('%-55s %s' % (name, '; '.join(res)), flush=True)
