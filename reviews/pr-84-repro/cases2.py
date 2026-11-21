def Q(n): return '> ' * n
PAD = [('pad %d' % i, 6, 20, ['a'], ['redraw']) for i in range(11)]   # test_core_markdown.c's known() goes by number: 9, 10, 11
CASES = PAD + [
 ('Q1 task at byte 120', 12, 50, ['a', Q(60) + '- [ ] task', 'end'], ['wrap', 'k:1G', 'redraw']),
 ('Q2 task at byte 240', 12, 50, ['a', Q(120) + '- [ ] task', 'end'], ['wrap', 'k:1G', 'redraw']),
 ('Q3 task at byte 260', 12, 50, ['a', Q(130) + '- [ ] task', 'end'], ['wrap', 'k:1G', 'redraw']),
 ('Q4 bullet with blanks at byte 260', 12, 50, ['a', Q(130) + '-   x', Q(130) + '# head', Q(130) + '3. x', 'end'], ['wrap', 'k:1G', 'redraw']),
 ('Q5 marker below the window', 7, 50, ['a', Q(140) + '- [ ] task', 'end'], ['wrap', 'k:1G', 'redraw']),
 ('Q6 marker right of the window', 7, 50, ['a', Q(40) + '- [ ] task', Q(40) + '3. x', Q(40) + '- y', ' ' * 60 + '```c', 'x', ' ' * 60 + '```', 'end'], ['k:1G', 'redraw']),
 ('Q7 fence at byte 260', 14, 50, ['a', Q(130) + '```c x', Q(130) + 'int a;', Q(130) + '```', 'end'], ['wrap', 'k:1G', 'redraw']),
 ('Q8 code in list and tabs', 18, 50, ['- ```c', '  int a;', '', '  \tb', '  ```', '-\ta', '\t- b', '- [ ]\tc', '-  [x]  d', '1.  [-] e', 'end'], ['k:11G', 'redraw']),
 ('Q9 code wrap long', 18, 30, ['```c', 'x' * 70, '', '\t' + 'y' * 40, '```', 'end'], ['wrap', 'k:6G', 'redraw']),
 ('Q10 invalid and wide in info', 12, 40, ['```c 漢字 ́x', 'a', '```', '```' + '漢' * 30, 'b', '```', 'end'], ['k:7G', 'redraw']),
 ('Q11 two markers', 12, 40, ['- - a', '- 1. b', '1. - c', '- - [ ] d', '- - # e', 'end'], ['k:6G', 'redraw']),
]
