#!/usr/bin/env python3
"""PR 165 re-review: C-] places the key cannot reach, beyond the PR's edges(),
recorded with Neovim as test_lsp_locs.py records.  usage (repo root):
python3 reviews/pr-165-repro/edges2.py OUT; then build/.../test_lsp_locs OUT"""
import json, sys
sys.path.insert(0, 'tests')
import test_lsp_locs as T
def at(f, line, col=0): return {'uri': 'ROOT/' + f, 'range': {'start': {'line': line, 'character': col}, 'end': {'line': line, 'character': col}}}
e = [('upper-case guess (ignorecase in jumpto_tag)', '<C-]>', 'x = bar(1);\n', [0, 5], 'x\nBAR (int a)\n', at('o.c', 9)),
     ('guess matches the line C-] was typed on', '<C-]>', 'x = bar(1);\nint bar(int a)\n', [0, 5], '', at('m.c', 9)),
     ('column past the line end', '<C-]>', 'x = bar(1);\n', [0, 5], 'int bar(int a)\n', at('o.c', 0, 30)),
     ('column at the line end', '<C-]>', 'x = bar(1);\n', [0, 5], 'int bar\n', at('o.c', 0, 7)),
     ('non-ASCII keyword', '<C-]>', 'x = bär(1);\n', [0, 5], 'x\nint bär (int a)\n', at('o.c', 9)),
     ('keyword after the cursor', '<C-]>', '  (bar);\n', [0, 0], 'int bar(int a)\n', at('o.c', 0, 4)),
     ('no keyword on the line', '<C-]>', '  ();\n', [0, 0], 'int bar(int a)\n', at('o.c', 0, 4)),
     ('gd one place past the end', 'gd', 'x = bar(1);\n', [0, 5], 'x\n', at('o.c', 5)),
     ('grt in a file not there', 'grt', 'x = bar(1);\n', [0, 5], '', at(T.GONE, 0)),
     ('gO error answer', 'gO', 'x = bar(1);\n', [0, 5], '', None)]
method = {'<C-]>': 'textDocument/definition', 'gd': 'textDocument/definition', 'grt': 'textDocument/typeDefinition', 'gO': 'textDocument/documentSymbol'}
cases = []
for i, (why, key, m, cur, o, res) in enumerate(e):
    c = {'k': i, 'kind': 'loc', 'files': {'m.c': m, 'o.c': o}, 'method': method[key], 'result': res, 'key': key, 'cur': cur}
    if res is None: c['error'] = 'boom'
    cases.append(c)
out = T.run_nvim(cases)
with open(sys.argv[1], 'w') as f:
    for (why, *_), c, o in zip(e, cases, out):
        c.update(o)
        print(why, '->', {k: o.get(k) for k in ('msg', 'at', 'list')}, file=sys.stderr)
        f.write(json.dumps(c, ensure_ascii=False) + '\n')
