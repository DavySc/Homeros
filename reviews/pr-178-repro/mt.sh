#!/bin/bash
# mt.sh K 'text': does mutant K (line K of rv/my-plants.txt, from 0) answer differently from the tree on the text (every character)?  and does the tree agree with Org there?
printf "$2" > rv/m.org
python3 -c "
b=open('rv/m.org','rb').read(); open('rv/m.org.cmd','w').write(' '.join(str(j) for j in range(len(b)+1) if j==len(b) or (b[j]&0xc0)!=0x80))"
m=/tmp/claude-0/mu/m$(printf %03d $1)/x
d=$(diff <(rv/drv_o2 -o rv/m.org) <($m -o rv/m.org) | grep -c '^<')
first=$(diff <(rv/drv_o2 -o rv/m.org) <($m -o rv/m.org) | grep '^[<>]' | head -2 | tr '\n' ' ')
echo "mutant $1 ($(sed -n "$(($1+1))p" rv/my-plants.txt | awk -F"§" "{print \$1}")): differs from the tree at $d positions $first"
rv/all.sh "$2" | tail -n 3
