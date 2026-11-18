# second pass: where Neovim draws the overlay in a window that is not current and is scrolled
# sideways (or shows its top line from skipcol), for a marker that is not at column 0; the
# row-end cut of the overlay; '>' and '<' filler cells
H = ['# One', '   ### abcdefghijklmnop', '> ## quoted heading text', '- # in a list item', '> > #### deep', '      ## not (code)', 'end']
def side(n): return ('side9 leftcol %d' % (n - 1), 20, 60, ['x' * (25 + n)] + H, ['vs', 'k:1G$', 'w:h', 'redraw'])
LONG = '## ' + 'word ' * 60
CASES = [side(n) for n in (2, 3, 4, 5, 6, 8, 12, 40)] + [
 ('skip9 indent', 10, 60, ['a', '   ' + LONG, 'b'], ['wrap', 'vs', 'e:o.txt', 'w:h', 'k:2G$', 'w:l', 'redraw']),
 ('skip9 quote', 10, 60, ['a', '> ' + LONG, 'b'], ['wrap', 'vs', 'e:o.txt', 'w:h', 'k:2G$', 'w:l', 'redraw']),
 ('skip9 col0', 10, 60, ['a', LONG, 'b'], ['wrap', 'vs', 'e:o.txt', 'w:h', 'k:2G$', 'w:l', 'redraw']),
 ('skip9 col0 3-digit', 10, 60, ['l%d' % i for i in range(120)] + ['## ' + 'word ' * 70, 'b'], ['wrap', 'vs', 'e:o.txt', 'w:h', 'k:121G$', 'w:l', 'redraw']),
 ('rowend wrap 1', 12, 16, ['a', '      ###### wo', 'b', '       ##### wo', 'c'], ['wrap', 'k:5G', 'redraw']),
 ('rowend wrap 2', 12, 16, ['a', '> > > ###### ' + 'word ' * 6, 'b', '> > > > ### x', '> > > > # yy', 'c'], ['wrap', 'k:1G', 'redraw']),
 ('rowend wrap 3', 12, 17, ['a', '> > > > > ## x', '> > > > > # yy', '> > > > >  # z', 'c'], ['wrap', 'k:1G', 'redraw']),
 ('rowend nowrap', 12, 16, ['a', '> > > ###### wo', '> > > > ### x', '> > > > > # yy', 'c'], ['k:1G', 'redraw']),
 ('rowend eol', 12, 16, ['a', '> > > > ####', '> > > > > ##', '> > > > > #', '> > > > >  #', 'c'], ['wrap', 'k:1G', 'redraw']),
 ('filler > wrap', 20, 30, ['x', '# ' + 'a' * 23 + '漢字' * 20, 'plain ' + 'a' * 19 + '漢字' * 3, 'y'], ['wrap', 'k:1G', 'redraw']),
 ('filler > nowrap', 20, 30, ['x', '# ' + 'a' * 23 + '漢字', 'plain ' + 'a' * 19 + '漢字' * 3, 'y'], ['k:1G', 'redraw']),
 ('filler < leftcol', 20, 60, ['x' * 27, '# 漢字 heading', '漢字 plain', '## a漢字'], ['vs', 'k:1G$', 'w:h', 'redraw']),
 ('filler < leftcol 2', 20, 60, ['x' * 29, '# 漢字 heading', '漢字 plain', '## a漢字'], ['vs', 'k:1G$', 'w:h', 'redraw']),
 ('C: after :e, then enter and leave', 20, 60, H, ['k:2G', 'vs', 'e:o.txt', 'w:w', 'w:w', 'redraw']),
 ('C: after :e', 20, 60, H, ['k:2G', 'vs', 'e:o.txt', 'redraw']),
 ('E: setext inline start', 20, 50, ['x', '', '*em* # b', '===', '', '`c` ## c', '---', '', '*1* # d', '===', '', '    # four', '===', 'y'], ['k:1G', 'redraw']),
]
