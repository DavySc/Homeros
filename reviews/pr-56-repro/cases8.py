# eighth round: every difference of rounds 1 to 7 again with CTRL-L before the
# screen is read, since Neovim's screen was found stale once (round 7, case 3:
# no extmark on the line, yet the other window still showed it rendered)
import runpy, os
here = os.path.dirname(os.path.abspath(__file__))
pick = {'cases1.py': ['wide at edge', 'wrap wide odd', 'vs leftcol back to other'],
        'cases2.py': ['side 2', 'side 3', 'side 5', 'side 30', 'side cur left on heading', 'side sp', 'cut wide nowrap h1',
                      'wrap wide filler', 'wrap wide filler h3 quote', 'other buffer current', 'other buffer insert', 'two md buffers',
                      'open line under heading'],
        'cases3.py': ['leave by CTRL-W', 'leave by :e in a split', 'o Esc in one send', 'O Esc above'],
        'cases4.py': ['ordered list 6 to 9', 'nested 8', 'setext emphasis start'],
        'cases7.py': ['skipcol other buffer', 'skipcol other buffer, 3-digit number', 'skipcol same buffer cursor line', 'skipcol current']}
CASES = []
for f, names in pick.items():
    for c in runpy.run_path(os.path.join(here, f))['CASES']:
        if c[0] in names: CASES.append((c[0] + ' + CTRL-L', c[1], c[2], c[3], c[4] + ['redraw']))
