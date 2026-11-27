#!/bin/bash
# build2.sh NAME [WORD]: scratch copy of the checked-out tree, the one "sql:"
# plant of tests/plants/sql.txt whose name holds WORD applied (none: clean),
# test_sql built as rv/NAME with the Makefile's own command, and NAME.iso,
# whose main() runs only cancel_tokens() and close_notify() (no server)
set -e
name=$1 word=$2
tmp=/tmp/claude-0/rv/tree-$name
rm -rf $tmp; mkdir $tmp; cd /home/claude/wt159
cp -r Makefile assets include ref src tests tools build $tmp/
cd $tmp
python3 - "$word" <<'PY'
import sys
w=sys.argv[1]
if w:
    line=[l for l in open('tests/plants/sql.txt') if l.startswith('sql:') and w in l.split('~')[0]]
    assert len(line)==1, line
    name,target,prep,file,old,new=line[0].rstrip('\n').split('~')
    old,new=old.replace('¶','\n'),new.replace('¶','\n')
    s=open(file).read(); assert s.count(old)==1
    open(file,'w').write(s.replace(old,new)); print('planted:',name)
PY
cmd=$(cat /tmp/claude-0/rv/cmd.txt)
eval "${cmd/-o build\/san\/test_sql/-o /tmp/claude-0/rv/$name}"
printf '#define main real_main\n#include "test_sql.c"\n#undef main\nint main(void){ (void)real_main; cancel_tokens(); close_notify(); puts("iso ok"); return 0; }\n' > tests/iso.c
c2=${cmd/tests\/test_sql.c/tests\/iso.c}
eval "${c2/-o build\/san\/test_sql/-o /tmp/claude-0/rv/$name.iso}"
echo built $name
