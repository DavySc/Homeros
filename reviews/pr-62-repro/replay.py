#!/usr/bin/env python3
"""Replay the cases of a recorded tests/test_core_screens.txt through live
Neovim and write them again in the same format.
usage: replay.py RECORDER.py IN.txt OUT.txt MODE
MODE pr: the recorder's own start (its LUA, run through -c).
MODE start: no -c at all: settings through --cmd, the Lua from a VimEnter
autocommand, and the recorder's 'normal! 1G^' line removed, so the cursor
is where Neovim's own start left it."""
import concurrent.futures, json, os, subprocess, sys, tempfile
rec, src, dst, mode = sys.argv[1:5]
code = open(rec).read().rsplit('\nmain()', 1)[0]
g = {}
exec(compile(code, rec, 'exec'), g)
LUA = g['LUA']
if mode == 'start':
    assert "vim.cmd('normal! 1G^')\n" in LUA
    LUA = LUA.replace("vim.cmd('normal! 1G^')\n", '')

def keys(t):
    return [] if t == 'nop' else g['nvim_keys'](t)

def run(case):
    d = tempfile.mkdtemp()
    for i, t in enumerate(case['files']):
        with open(os.path.join(d, 'f%d' % i), 'wb') as f: f.write(t)
    fi, fo, fl = d + '/in.json', d + '/out.json', d + '/t.lua'
    json.dump({'rows': case['rows'], 'cols': case['cols'],
               'keys': [k if isinstance(k, list) else [k] for k in map(keys, case['tokens'])]}, open(fi, 'w'))
    open(fl, 'w').write('local IN, OUT = %r, %r\n' % (fi, fo) + LUA)
    if mode == 'start':
        cmd = ['nvim', '--clean', '--headless', '--cmd', g['SETUP'], '--cmd', 'autocmd VimEnter * ++once luafile ' + fl, 'f0']
    else:
        cmd = ['nvim', '--clean', '--headless', '-c', g['SETUP'], '-c', 'luafile ' + fl, 'f0']
    subprocess.run(cmd, cwd=d, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    return json.load(open(fo))

cases, c = [], None
for line in open(src, encoding='utf-8'):
    line = line.rstrip('\n')
    if line.startswith('C '):
        _, r, co = line.split(); c = {'rows': int(r), 'cols': int(co), 'files': [], 'tokens': []}; cases.append(c)
    elif line.startswith('F'): c['files'].append(bytes.fromhex(line[2:]))
    elif line.startswith('T '): c['tokens'].append(line[2:])
with concurrent.futures.ThreadPoolExecutor(4) as ex:
    for c, s in zip(cases, ex.map(run, cases)):
        c['screens'], c['marks'] = s['screens'], s['marks']
        c['files'] = [f.decode() for f in c['files']]
g['write_cases'](dst, cases)
print(len(cases), 'cases replayed, mode', mode)
