# third round: how the other buffer was left; text changed in insert mode
H = ['# One', 'text', '### Three', '  ## ind', '> # q', '- ## li', '###### Six six six', 'end']
CASES = [
 ('leave by CTRL-W', 20, 60, H, ['k:3G', 'vs', 'e:o.txt', 'w:h', 'k:5G', 'w:l']),
 ('leave by :e in a split', 20, 60, H, ['k:3G', 'vs', 'e:o.txt']),
 ('o Esc in one send', 20, 50, H, ['k:1Go<Esc>']),
 ('o then Esc', 20, 50, H, ['k:1Go', 'k:<Esc>']),
 ('o text Esc', 20, 50, H, ['k:1Goabc<Esc>']),
 ('o text Esc then j', 20, 50, H, ['k:1Goabc<Esc>', 'k:j']),
 ('O Esc above', 20, 50, H, ['k:2GO<Esc>']),
 ('i x Esc off heading', 20, 50, H, ['k:2Gix<Esc>']),
 ('i x Esc then wait keyless', 20, 50, H, ['k:2Gix', 'k:<Esc>']),
 ('A on other line Esc', 20, 50, H, ['k:2GA more<Esc>']),
 ('x off heading', 20, 50, H, ['k:2Gx']),
 ('dd off heading', 20, 50, H, ['k:2Gdd']),
 ('dd then j', 20, 50, H, ['k:2Gdd', 'k:j']),
 ('new heading typed, esc, k', 20, 50, ['a', 'b', 'c'], ['k:3Go## new<Esc>', 'k:k']),
 ('new heading typed, esc', 20, 50, ['a', 'b', 'c'], ['k:1Go## new<Esc>']),
 ('u after dd', 20, 50, H, ['k:2Gdd', 'k:u']),
 ('same buf vs insert in one', 20, 60, H, ['vs', 'k:2Gi']),
 ('same buf vs cmdline', 20, 60, H, ['vs', 'k:2G', 'k::']),
]
