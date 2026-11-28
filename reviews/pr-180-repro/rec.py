#!/usr/bin/env python3
"""PR 180's own recorder (tests/test_lsp_locs.py run_nvim) on given hover values, each asked REP times in one Neovim.
usage: RM_RTP=... rec.py TESTS_DIR REP OUT.txt VALUE..."""
import sys, json
sys.path.insert(0, sys.argv[1])
import test_lsp_locs as t
rep, outp, vals = int(sys.argv[2]), sys.argv[3], sys.argv[4:]
cases = []
for v in vals:
    for _ in range(rep):
        cases.append({'k': len(cases), 'kind': 'hover', 'files': {'m.c': 'int x;\nint y;\nint foo(int x);\n'}, 'cur': [2, 4],
                      'result': {'contents': {'kind': 'markdown', 'value': v.encode().decode('unicode_escape')}}})
out = t.run_nvim(cases)
with open(outp, 'w') as f:
    for c, o in zip(cases, out):
        f.write(json.dumps(dict(c, **o)) + '\n')
        print(c['k'], 'at', o.get('at'), 'msg', o.get('msg'), 'lines', len(o.get('lines', [])))
