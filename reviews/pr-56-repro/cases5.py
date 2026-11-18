# fifth round: another buffer's window scrolled sideways; a wrapped heading cut at the bottom of a window above another
H = ['# One', 'text', '### Three', '  ## ind', '> # q', '- ## li', '###### Six six six', 'end']
LONG = '## ' + 'word ' * 30
CASES = [
 ('other buffer scrolled sideways', 20, 60, ['x' * 80] + H, ['vs', 'e:o.txt', 'w:h', 'k:1G$', 'w:l']),
 ('other buffer scrolled then back', 20, 60, ['x' * 80] + H, ['vs', 'e:o.txt', 'w:h', 'k:1G$', 'w:l', 'w:h']),
 ('wrap cut bottom above a window', 24, 30, ['a', 'b', 'c', LONG * 4, 'y'], ['wrap', 'sp']),
 ('wrap cut bottom above, top current', 24, 30, ['a', 'b', 'c', LONG * 4, 'y'], ['wrap', 'sp', 'w:k']),
 ('wrap cut bottom, vs', 12, 60, ['a', 'b', 'c', LONG * 4, 'y'], ['wrap', 'vs']),
 ('heading top line cut by skipcol in split', 24, 30, ['a', LONG * 4, 'y', 'z', 'q', 'r', 's', 't', 'u', 'v'], ['wrap', 'sp', 'k:10G']),
]
