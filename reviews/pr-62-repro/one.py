import sys, json
code = open('../Homeros/tests/test_core_screens.py').read().rsplit('\nmain()', 1)[0]
g = {}; exec(compile(code, 'rec', 'exec'), g)
c = {'rows': 12, 'cols': 40, 'files': [sys.argv[2]], 'tokens': sys.argv[3:]}
s = g['run_nvim'](c); c['screens'], c['marks'] = s['screens'], s['marks']
g['write_cases'](sys.argv[1], [c])
