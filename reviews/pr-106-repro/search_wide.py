#!/usr/bin/env python3
"""The PR's recorder (tests/test_core_search.py) with wider patterns: empty
matches (x*, \\zs, \\ze), classes, matches over several lines, \\n runs.
usage: PYTHONPATH=tests python3 search_wide.py N SEED --record FILE"""
import sys, random
import test_core_search as t

def gen_pat(r, lines):
    s = r.choice([x for x in lines if x] or ['a'])
    a = r.randrange(len(s))
    ch = s[a]
    if ord(ch) < 32 or ord(ch) == 127 or ch in '\\^$.*[]~/': ch = 'a'
    return r.choice([ch + '*', '\\zs' + ch, ch + '\\ze', '.', '\\s\\+', '\\s*', ch + '\\_.\\{-}' + ch, '\\n\\n', '\\n', ch + '\\n.',
                     '\\_s\\+', '.$', '^.', '\\<', '\\>', '\\w\\+', ch + '\\_.*', '\\n\\zs', '$\\n^', '[^a]', '\\%$', '\\%^', '^$', '\\t', '..'])
t.gen_pat = gen_pat
t.main()
