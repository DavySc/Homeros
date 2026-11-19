# PR 64 review: the colour of each word in Davy's SSMS screenshot, read from
# the pixels that have exactly one of SSMS's dark-theme text colours (so
# ClearType's fringes do not vote).  Prints, per text line, x:class for each
# run of coloured columns.  K keyword blue, F function magenta, o operator
# grey, . plain, N number.
#   python3 reviews/pr-64-repro/colours.py /mnt/project-files/syntax/ssms-screenshot-2.png
import sys
from collections import Counter
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
ex = {(86, 156, 214): 'K', (201, 117, 213): 'F', (129, 129, 129): 'o', (220, 220, 220): '.', (181, 206, 168): 'N'}
bands = [[58, 70], [75, 85], [93, 103], [112, 124], [130, 139], [148, 158], [166, 175], [182, 194], [201, 211], [220, 229]]
for i, (y0, y1) in enumerate(bands):
    out, cur, start, gap = [], Counter(), None, 0
    for x in range(80, im.width):
        c = Counter(ex[p] for p in (im.getpixel((x, y)) for y in range(y0 - 2, y1 + 3)) if p in ex)
        if c:
            if start is None: start = x
            cur += c; gap = 0
        else:
            gap += 1
            if start is not None and gap >= 6:
                out.append('%d:%s' % (start, cur.most_common(1)[0][0])); cur, start = Counter(), None
    print(i + 2, ' '.join(out))
