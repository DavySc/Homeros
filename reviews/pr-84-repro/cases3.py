def Q(n): return '> ' * n
PAD = [('pad %d' % i, 6, 20, ['a'], ['redraw']) for i in range(11)]   # test_core_markdown.c's known() goes by number: 9, 10, 11
CASES = PAD + [
 ('R1 task below the window, text width 32', 5, 36, ['a', Q(40) + '- [ ] task', 'end'], ['wrap', 'k:1G', 'redraw']),
 ('R2 bullet below the window, text width 32', 5, 36, ['a', Q(40) + '- item', 'end'], ['wrap', 'k:1G', 'redraw']),
 ('R3 number below the window, text width 16', 6, 20, ['a', 'b', Q(20) + '3. item', 'end'], ['wrap', 'k:1G', 'redraw']),
 ('R4 task below the window, text width 30', 5, 34, ['a', Q(40) + '- [ ] task', 'end'], ['wrap', 'k:1G', 'redraw']),
]
