PAD = [('pad %d' % i, 6, 20, ['a'], ['redraw']) for i in range(11)]   # test_core_markdown.c's known() goes by number: 9, 10, 11
CASES = PAD + [
 ('S1 tab three cells wide in the info string', 10, 40, ['```cdefg\tx', 'a', '```', '```c defghijklm\ty', 'b', '```', '```c d\tz', 'c', '```', 'end'], ['k:10G', 'redraw']),
 ('S2 the same with wrap', 10, 40, ['```cdefg\tx', 'a', '```', '```c defghijklm\ty', 'b', '```', '```c d\tz', 'c', '```', 'end'], ['wrap', 'k:10G', 'redraw']),
 ('S3 wide character at the row end, wrap', 12, 30, ['```c ' + 'a' * 19 + '漢字', 'a', '```', '```c ' + 'a' * 20 + '漢字', 'b', '```', '```c ' + 'a' * 21 + '漢字', 'c', '```', 'end'], ['wrap', 'k:10G', 'redraw']),
 ('S4 wide character at the row end, no wrap', 12, 30, ['```c ' + 'a' * 19 + '漢字', 'a', '```', '```c ' + 'a' * 20 + '漢字', 'b', '```', '```c ' + 'a' * 21 + '漢字', 'c', '```', 'end'], ['k:10G', 'redraw']),
 ('S5 tab and control at the row end', 12, 30, ['```c ' + 'a' * 18 + '\tz', 'a', '```', '```c ' + 'a' * 20 + '\x01z', 'b', '```', '```c ' + 'a' * 21 + '\x01z', 'c', '```', 'end'], ['wrap', 'k:10G', 'redraw']),
 ('S6 indented fence with a tab in the info', 10, 40, ['   ```c\tx', '   a', '   ```', '> ```c\ty', '> b', '> ```', 'end'], ['k:7G', 'redraw']),
 ('S7 tab in info, view scrolled sideways', 12, 60, ['x' * 34, '```cdefg\tx' + ' y' * 5, 'a', '```'], ['vs', 'k:1G$', 'w:h', 'redraw']),
]
