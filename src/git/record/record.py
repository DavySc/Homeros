#!/usr/bin/env python3
"""Record magit (evil-collection keys) on the scratch repository: for each
step of a scenario, send keys to `emacs -nw` in tmux, wait until Emacs and
git are idle, and keep the selected window's rows, its buffer name, point and
the echo area, and the repository's state.  Usage: record.py SCENARIOS OUT"""
import json, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
EMACS = os.environ.get('EMACS_BIN', '/home/claude/emacs-nox/bin/')
REPO = os.environ.get('REC_REPO', '/tmp/hg-scratch/repo')
W, H = 100, 60
ENV = dict(os.environ, GIT_CONFIG_GLOBAL='/dev/null', GIT_CONFIG_NOSYSTEM='1',
           GIT_AUTHOR_NAME='Tester', GIT_AUTHOR_EMAIL='t@example.com',
           GIT_COMMITTER_NAME='Tester', GIT_COMMITTER_EMAIL='t@example.com',
           GIT_AUTHOR_DATE='2026-01-02T00:00:00Z', GIT_COMMITTER_DATE='2026-01-02T00:00:00Z',
           LANG='C.UTF-8', TERM='xterm-256color')

def sh(*a, **k):
    return subprocess.run(a, env=ENV, capture_output=True, text=True, **k).stdout

def tmux_key(k):
    names = {'<Tab>': 'Tab', '<CR>': 'Enter', '<Esc>': 'Escape', '<BS>': 'BSpace',
             '<C-j>': 'C-j', '<C-k>': 'C-k', '<C-c>': 'C-c', '<C-d>': 'C-d', '<C-u>': 'C-u',
             '<Space>': 'Space', '<M-j>': 'M-j', '<M-k>': 'M-k'}
    if k in names: sh('tmux', 'send-keys', '-t', 'rec', names[k])
    else: sh('tmux', 'send-keys', '-t', 'rec', '-l', k)

def split(keys):
    out, i = [], 0
    while i < len(keys):
        if keys[i] == '<':
            j = keys.index('>', i); out.append(keys[i:j + 1]); i = j + 1
        else: out.append(keys[i]); i += 1
    return out

def state():
    while True:
        r = subprocess.run([EMACS + 'emacsclient', '--eval', '(rec-state)'], env=ENV, capture_output=True, text=True)
        if r.returncode == 0: return r.stdout.strip()[1:-1]
        time.sleep(0.2)

def settle():
    last, t0 = None, time.time()
    while True:
        if time.time() - t0 > 30: raise RuntimeError('step never settled: ' + repr(last))
        time.sleep(0.4)
        s = state()
        scr = sh('tmux', 'capture-pane', '-p', '-t', 'rec')
        cur = sh('tmux', 'display', '-p', '-t', 'rec', '#{cursor_y} #{cursor_x}')
        if not s.startswith('busy') and (s, scr, cur) == last: return s, scr, cur
        last = (s, scr, cur)

def repo_state():
    g = lambda *a: sh('git', '-C', REPO, *a)
    return {'status': g('status', '--porcelain=v2', '--branch', '--untracked-files=all'),
            'refs': g('for-each-ref', '--format=%(refname) %(objectname:short) %(upstream:short)'),
            'head': g('symbolic-ref', '-q', 'HEAD'),
            'log': g('log', '--all', '--format=%h %p %s'),
            'stash': g('stash', 'list'),
            'cached': g('diff', '--cached'), 'worktree': g('diff')}

def record(name, sc):
    steps = sc['keys']
    subprocess.run([os.path.join(HERE, '..', 'mkrepo.sh'), REPO], env=ENV, check=True, capture_output=True)
    subprocess.run(['sh', '-ec', sc.get('setup', '')], cwd=REPO, env=ENV, check=True, capture_output=True)
    sh('tmux', 'kill-server'); time.sleep(0.5)
    cmd = (f"cd {REPO} && {EMACS}emacs -nw -Q -l {HERE}/init.el "
           f"--eval '(progn (magit-status \"{REPO}/\") (delete-other-windows))'")
    subprocess.run(['tmux', 'new-session', '-d', '-s', 'rec', '-x', str(W), '-y', str(H), cmd], env=ENV, check=True)
    out = []
    for keys in [''] + steps:
        if keys.startswith('$ '):
            subprocess.run(['sh', '-ec', keys[2:]], cwd=REPO, env=ENV, check=True, capture_output=True)
        else:
            for k in split(keys): tmux_key(k); time.sleep(0.05)
        s, scr, cur = settle()
        rows = scr.split('\n')
        cy, cx = map(int, cur.split())
        wins = []
        for w in s.split(';')[1:]:
            top, bot, buf, sel = w.split('|')
            top, bot = int(top), int(bot)
            lines = [r.rstrip() for r in rows[top:bot]]
            while lines and not lines[-1]: lines.pop()
            wins.append({'buffer': buf, 'lines': lines, 'selected': sel == '*',
                         'cursor': [cy - top, cx] if top <= cy < bot else None})
        out.append({'keys': keys, 'windows': wins, 'echo': rows[H - 1].rstrip(), 'repo': repo_state()})
        print(name, repr(keys), [(w['buffer'], w['cursor']) for w in wins], '|', rows[H - 1].rstrip(), file=sys.stderr, flush=True)
    sh('tmux', 'kill-server')
    return {'setup': sc.get('setup', ''), 'steps': out}

if __name__ == '__main__':
    sc = json.load(open(sys.argv[1]))
    only = sys.argv[3:]
    res = json.load(open(sys.argv[2])) if os.path.exists(sys.argv[2]) else {}
    for n, st in sc.items():
        if only and n not in only: continue
        try: res[n] = record(n, st)
        except RuntimeError as x: print(n, 'FAILED', x, file=sys.stderr, flush=True); sh('tmux', 'kill-server'); continue
        json.dump(res, open(sys.argv[2], 'w'), indent=1, ensure_ascii=False)
