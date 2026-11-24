import sys
src=open(sys.argv[1],'rb').read(); rows=[l.split() for l in open(sys.argv[2])]
col=[i for i,r in enumerate(rows) if r[1]!='0']
print('  coloured bytes: %d; the last on line %d of %d' % (len(col), src[:col[-1]].count(b'\n')+1 if col else 0, src.count(b'\n')))
