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
]
