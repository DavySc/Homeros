Q = '> ' * 140
CASES = [
 ('P1 ordinal wider than marker', 18, 50, ['1. item %d' % i for i in range(1, 13)] + ['', 'end'], ['k:14G', 'redraw']),
 ('P2 hundred items', 18, 50, ['1. item %d' % i for i in range(1, 104)] + ['', 'end'], ['k:105G', 'redraw']),
 ('P3 empty ordered items', 18, 50, ['1.' for i in range(1, 13)] + ['', 'end'], ['k:14G', 'redraw']),
 ('P4 paren and zeros', 18, 50, ['7) a', '8) b', '', '007. a', '008. b', '', '0. a', '0. b', '', '1) a', '1) b', '', 'end'], ['k:13G', 'redraw']),
 ('P5 info with tab and ctrl', 18, 50, ['```c\tx y', 'int a;', '```', '', '```c \x01z', 'b', '```', '', '   ```  lua  ', '   x', '   ```', 'end'], ['k:12G', 'redraw']),
 ('P6 long info', 18, 50, ['```c ' + 'w' * 300, 'int a;', '```', 'end'], ['k:4G', 'redraw']),
 ('P7 long info wrap', 18, 50, ['```c ' + 'w' * 300, 'int a;', '```', 'end'], ['wrap', 'k:4G', 'redraw']),
 ('P8 marker past the window', 10, 50, ['a', Q + '- [ ] task', 'end'], ['wrap', 'k:1G', 'redraw']),
 ('P9 marker past byte 256', 20, 50, ['a', Q + '- [ ] task', Q + '# head', Q + '3. x', 'end'], ['wrap', 'k:1G', 'redraw']),
 ('P10 code in list and tabs', 18, 50, ['- ```c', '  int a;', '', '  \tb', '  ```', '-\ta', '\t- b', '- [ ]\tc', '-  [x]  d', '1.  [-] e', 'end'], ['k:11G', 'redraw']),
 ('P11 code wrap long', 18, 30, ['```c', 'x' * 70, '', '\t' + 'y' * 40, '```', 'end'], ['wrap', 'k:6G', 'redraw']),
 ('P12 heading in list in quote', 18, 50, ['> - # h', '> 1. ## h', '>   - [ ] ### no', '- - a', '- 1. b', '1. - c', 'end'], ['k:7G', 'redraw']),
 ('P13 narrow', 12, 12, ['# head', '- [ ] task', '```c x', 'int a;', '```', '10. x', 'end'], ['vs', 'vs', 'k:7G', 'redraw']),
 ('P14 unicode info', 12, 40, ['```é‍漢 x', 'a', '```', '~~~ c', 'b', '~~~', 'end'], ['k:7G', 'redraw']),
]
