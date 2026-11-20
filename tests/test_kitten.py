#!/usr/bin/env python3
"""Records the save kitten from Davy's my-nvim (tests/test_kitten.lua) as
tests/test_kitten.txt, which tests/test_kitten.c replays without Neovim.
usage: KITTEN=path/to/my-nvim/lua/config/core/kitten.lua test_kitten.py
Recorded with my-nvim 695e3d7 and Neovim 0.12.6.  A case is a screen size
and a file name; a frame is the three rows the kitten takes, each cell's
text in order (a double-width character's right half is empty), ended
by "|"."""
import os, subprocess, sys

CASES = [(12, 40, 'notes.org'), (24, 80, 'main.c'), (8, 40, '全角.md'), (6, 30, 'é-ü.txt'),
         (10, 20, 'notes.org'), (8, 40, 'a' * 60), (7, 12, 'x'), (6, 20, '全全全'), (6, 21, '全全全'), (6, 12, 'abc')]

out = ['# The save kitten as my-nvim draws it (tests/test_kitten.py); do not edit.']
for r, c, name in CASES:
    env = dict(os.environ, R=str(r), C=str(c), NAME=name)
    p = subprocess.run(['nvim', '--headless', '--clean', '-l', os.path.join(os.path.dirname(__file__), 'test_kitten.lua')],
                       env=env, capture_output=True, text=True, timeout=60)
    frames = [f.strip('\n') for f in p.stdout.split('\2') if f.strip('\n')]
    if len(frames) != 6: sys.exit('%dx%d %s: %d frames: %s' % (r, c, name, len(frames), p.stderr))
    out.append('case %d %d %s' % (r, c, name))
    for f in frames:
        out.append('frame')
        out += [''.join(row.split('\1')) + '|' for row in f.split('\n')]
open(os.path.join(os.path.dirname(__file__), 'test_kitten.txt'), 'w').write('\n'.join(out) + '\n')
