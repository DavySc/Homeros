# the area starts at the end of a line as wide as the window: records the cases for test_core_visual
import sys, itertools
sys.path.insert(0, 'tests')   # run from the repo root
import test_core_visual as t
text = 'abcdefghijklmnop\nx\n' + 'abcdefghijklmnop' * 2 + '\ny\nabcdefghijklmn全\nz\n'
cs = [{'rows': 10, 'cols': 20, 'wrap': w, 'text': text, 'mode': m, 'v': [l, 99], 'cur': [l + 1, 0], 'eol': False}
      for w in (False, True) for m in ('v', '<C-v>') for l in (1, 3, 5)]
it = iter(cs)
t.gen_case = lambda r: next(it)
sys.argv = ['x', str(len(cs)), '--record', sys.argv[1]]
t.main()
