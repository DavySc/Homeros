import sys, random
sys.path.insert(0, 'tests')   # run from the repo root
import test_core_visual as t
from test_core_screens import gen_text
W = ['foo', 'x', '\t', '\tb', 'é', 'λx', '世界', 'a世', '', '  ', 'bar_1', '(', ').', '𐐀x', 'éa', 'é', '\x01', '👍🏽', 'ｗｉｄｅ', '\t\t']
def gen_case(r):
    if r.random() < .5: text = ''.join(gen_text(r).splitlines(True)[:25])
    else: text = ''.join(' '.join(r.choice(W) for _ in range(r.randint(0, r.choice([3, 12, 40])))) + '\n' for _ in range(r.randint(1, 30)))
    n = text.count('\n') + 1
    pos = lambda: [r.randint(1, n), r.randint(0, r.choice([3, 12, 130]))]
    return {'rows': r.choice([4, 5, 7, 10, 24, 40]), 'cols': r.choice([18, 20, 33, 40, 80, 130]), 'wrap': r.random() < .5,
            'text': text, 'mode': r.choice(['v', 'V', '<C-v>']), 'v': pos(), 'cur': pos(), 'eol': r.random() < .15}
t.gen_case = gen_case
t.main()
