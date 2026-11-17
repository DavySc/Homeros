import sys, json, random, subprocess, os, tempfile
src = open(sys.argv[1]).read().rsplit('main()', 1)[0]
exec(src)
drv = sys.argv[2]; seed = int(sys.argv[3]); n = int(sys.argv[4])
def ours_b(c):
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f: f.write(c['text'])
    keys = ' '.join(str(CODES[k]) if k in CODES else ' '.join(str(ord(ch)) for ch in k) for k in c['keys'])
    p = subprocess.run([drv, f.name], input=keys.encode(), capture_output=True, timeout=20)
    os.unlink(f.name)
    if p.returncode: return {'error': p.returncode, 'stderr': p.stderr[-300:].decode('utf-8', 'replace')}
    try: return json.loads(p.stdout.decode('utf-8'))
    except Exception: return {'badutf8': p.stdout.decode('utf-8', 'backslashreplace')}
def differs(c):
    w = nvim([c])[0]; g = ours_b(c)
    return (g.get('lines') != w['lines'] or g.get('cursor') != w['cursor']), w, g
r = random.Random(seed)
cases = [{'text': gen_text(r), 'keys': gen_keys(r)} for _ in range(n)]
want = nvim(cases)
bad = [c for c, w in zip(cases, want) if (lambda g: g.get('lines') != w['lines'] or g.get('cursor') != w['cursor'])(ours_b(c))]
print(len(cases), 'cases,', len(bad), 'differ')
for c in bad[:int(sys.argv[5]) if len(sys.argv) > 5 else 3]:
    ks = c['keys']
    changed = True
    while changed:                      # drop keys one at a time while it still differs
        changed = False
        for i in range(len(ks)):
            t = {'text': c['text'], 'keys': ks[:i] + ks[i+1:]}
            if t['keys'] and differs(t)[0]: ks = t['keys']; changed = True; break
    d, w, g = differs({'text': c['text'], 'keys': ks})
    print('MIN text=%r keys=%s\n  nvim %r %r\n  ours %r' % (c['text'], ' '.join(ks), w['lines'], w['cursor'], g))
