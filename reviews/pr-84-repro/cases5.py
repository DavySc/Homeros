# Fifth... sixth pass probes: PR 95's cursor line under render-markdown's marks.
# 11 padding cases first (known() in tests/test_core_markdown.c goes by number).
L = ['# head', '- [ ] todo `code` x', '- [x] done', '- [-] custom box', '- bullet `c`', '  - nested', '1. one', '7. seven',
     '```c', 'int x;', '', '```', 'plain `inline` text', '## two `c`', '\t- [ ] tabbed', '> - [x] quoted box', 'end']
CASES = [('pad %d' % i, 6, 30, ['x'], ['redraw']) for i in range(11)]
# another window's cursor line on each line in turn; the current window's
# cursor on 'end' (so every line is rendered in both)
for n in range(1, len(L)):
    for sp in ('sp', 'vs'):
        CASES.append(('%s cursor line %d' % (sp, n), 40 if sp == 'sp' else 20, 60, L, [sp, 'k:%dG' % n, 'w:w', 'k:%dG' % len(L), 'redraw']))
# another buffer in the other window, cursor line on its boxes
for n in range(1, 8):
    CASES.append(('other buffer line %d' % n, 20, 60, ['x', 'y'], ['vs', 'e:g.md', 'k:%dG' % n, 'w:w', 'redraw']))
W = ['- [ ] ' + 'word ' * 30, '- [x] ' + '漢字' * 30, '1. [ ] ' + 'y' * 70, 'end']
for n in (1, 2, 3):
    CASES.append(('wrapped box %d' % n, 16, 40, W, ['wrap', 'sp', 'k:%dG' % n, 'w:w', 'k:4G', 'redraw']))
    CASES.append(('visual elsewhere box %d' % n, 16, 40, W, ['wrap', 'sp', 'k:%dG' % n, 'w:w', 'k:4G', 'visual']))
    CASES.append(('insert elsewhere box %d' % n, 16, 40, W, ['wrap', 'sp', 'k:%dG' % n, 'w:w', 'k:4G', 'k:i']))
# the current window's own cursor line (raw) with boxes rendered elsewhere
for n in (2, 3, 4, 15, 16):
    CASES.append(('own line %d' % n, 20, 60, L, ['k:%dG' % n, 'redraw']))
# the author's two named cases
CASES += [('another window on a tab', 10, 50, ['\tx', 'y', '# h'], ['k:0', 'vs', 'k:2G', 'visual']),
          ('a filler on the cursor line', 6, 50, ['漢字' * 30 + 'x' * 60, 'end'], ['k:141|'])]
OTHER = ['# h', '- [ ] a', '- [x] b ', '- [-] c ', '- d', '```lua', 'x = 1', '```']
