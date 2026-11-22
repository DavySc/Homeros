# Named cases for tests/test_core_markdown.py (its CASES.py): the review of
# PR 56's (but 'C: after :e', where render-markdown leaves the old window's
# cursor line raw until it is entered again, which the core does not keep,
# and the setext case, which the .lua does not draw), then more: a code
# block's filler cells ('<' takes its background, '>' does not; measured),
# overlays over backgrounds, nesting.  (name, rows, cols, [lines], [commands])
import os, runpy
CASES = runpy.run_path(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../reviews/pr-56-repro/cases9.py'))['CASES']
CASES = [c for c in CASES if c[0] not in ('C: after :e', 'E: setext inline start')]
F = ['```c', 'a' * 25 + '漢字', '## ' + 'a' * 22 + '漢字', 'b', '```', '## ' + 'a' * 22 + '漢字', 'end']
CASES += [
 # the right window scrolled sideways by '$' on line 1: one of the rows
 # puts '漢' across its leftcol
 ('code < leftcol', 16, 60, ['x' * 34, '```c'] + ['a' * k + '漢字' for k in range(6, 18)] + ['```'], ['vs', 'k:1G$', 'w:h', 'redraw']),
 ('code > nowrap', 12, 30, F, ['k:7G', 'redraw']),
 # an overlay takes its own background: the bullet's "0." left of the
 # heading's icon has none (measured)
 ('bullet under a heading background', 10, 60, ['x' * 32, '', '120. # heading text here', '', '- # h2'], ['vs', 'k:1G$', 'w:h', 'redraw']),
 # the icons cycle by nesting level
 ('nested bullets', 12, 40, ['- a', '  - b', '    - c', '      - d', '        - e', '          1. f', '          1. g', 'end'], ['k:8G', 'redraw']),
 ('code > wrap', 12, 30, F, ['wrap', 'k:7G', 'redraw']),
 # file text in the language line as the text draws it: an escape and a
 # bell as ^X, a tab as blanks (the review of PR 84, 1)
 ('info string controls', 10, 40, ['```c \x1b]0;PWNED\x07\tz', 'x', '```', '```c\tx y', 'y', '```', 'end'], ['k:7G', 'redraw']),
 # a marker below the window's last row, at a text width that is a power
 # of two, draws nothing (the review of PR 84, 2)
 ('box cut below', 5, 36, ['a', '> ' * 40 + '- [ ] task', 'end'], ['wrap', 'redraw']),
 ('bullet cut below', 5, 36, ['a', '> ' * 40 + '- item', 'end'], ['wrap', 'redraw']),
 ('number cut below', 5, 20, ['a', '> ' * 20 + '3. item', 'end'], ['wrap', 'redraw']),
 # "100." is wider than "1. ": Neovim puts it inline, before the marker;
 # the core leaves the marker as typed (known)
 ('ordered past the marker', 12, 40, ['1. item %d' % k for k in range(1, 104)], ['k:G', 'redraw']),
 # "10." as wide as "1. " is an overlay
 ('ordered as wide as the marker', 8, 30, ['1. item %d' % k for k in range(1, 13)], ['k:G', 'redraw']),
 # the review of PR 84's second pass: a tab 3 cells wide in the language
 # line; a double-width character that does not fit the row's end; text
 # before the language; an info string past 256 bytes; a fence and a
 # heading after a tab
 ('info tab of 3 cells', 16, 60, ['x' * 34, '```cdefg\tx y y y y y', 'a', '```'], ['vs', 'k:1G$', 'w:h', 'redraw']),
 ('info wide at the end', 10, 30, ['```c ' + 'a' * 21 + '漢字', 'x', '```', 'end'], ['k:4G', 'redraw']),
 ('info wide at the end, wrap', 10, 30, ['```c ' + 'a' * 21 + '漢字', 'x', '```', 'end'], ['wrap', 'k:4G', 'redraw']),
 ('info before the language', 10, 40, ['```{lua} x', 'y', '```', '``` {r} z', 'q', '```', 'end'], ['k:7G', 'redraw']),
 ('info past byte 256', 10, 50, ['```c ' + 'x' * 300, 'y', '```', 'end'], ['k:4G', 'redraw']),
 ('after a tab', 12, 40, ['-\t```c', '  x', '  ```', '>\t```lua', '>\tq', '>\t```', '1.\t## h', '>\t# h', '-\t# h', 'end'], ['k:10G', 'redraw']),
 ('a tab in a fence\'s indent', 12, 40, ['- a', '', '  \t```c', '  \tx', '  \t```', '1. d', '', '   \t```lua', 'end'], ['k:9G', 'redraw']),
 # Neovim puts the icon two cells further and over the text (known)
 ('a tab in a heading\'s indent', 12, 40, ['- b', '', '  \t# h', '- c', '', '   \t## h', 'end'], ['k:7G', 'redraw']),
 # a window wider than the 256 bytes draw_md reads: the language line
 # ends there (Neovim's goes on: known)
 ('info past byte 256, wide', 6, 300, ['```c ' + 'x' * 300, 'y', '```', 'end'], ['k:4G', 'redraw']),
 # PR 72's Visual tab rule: the window left on a Tab keeps the Normal
 # ruler while the other one is in Visual mode
 ('another window on a tab', 10, 50, ['\tx', 'y', '# h'], ['k:0', 'vs', 'k:2G', 'visual']),
 # PR 95's cursor line under a '<' filler (the view starts inside a
 # double-width character)
 ('a filler on the cursor line', 6, 50, ['漢字' * 30 + 'x' * 60, 'end'], ['k:141|']),
]
