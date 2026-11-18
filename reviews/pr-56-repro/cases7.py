# seventh round: a wrapped heading taller than its window, shown from skipcol ("<<<") in another buffer's window
L = ['a', '## ' + 'word ' * 60, 'b']
L3 = ['l%d' % i for i in range(120)] + ['## ' + 'word ' * 70, 'b']
CASES = [
 ('skipcol other buffer', 10, 60, L, ['wrap', 'vs', 'e:o.txt', 'w:h', 'k:2G$', 'w:l']),
 ('skipcol other buffer, 3-digit number', 10, 60, L3, ['wrap', 'vs', 'e:o.txt', 'w:h', 'k:121G$', 'w:l']),
 ('skipcol same buffer cursor line', 10, 60, L, ['wrap', 'vs', 'k:2G$']),
 ('skipcol current', 10, 40, L, ['wrap', 'k:2G$']),
]
