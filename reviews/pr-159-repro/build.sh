#!/bin/bash
# build.sh NAME PLANT(0|1|2) [base]: scratch copy of the PR head, the plant
# from tests/plants/sql.txt applied (1 = SqlRun firstdone, 2 = 3980), test_sql
# built as rv/NAME with the Makefile's own command; "base" takes main's
# tests/test_sql.c instead; without it also NAME.iso, which runs
# cancel_tokens() alone (no server)
set -e
name=$1 plant=$2 base=$3
tmp=/tmp/claude-0/rv/tree-$name
rm -rf $tmp; mkdir $tmp; cd /home/claude/wt159
cp -r Makefile assets include ref src tests tools build $tmp/
cd $tmp
if [ -n "$base" ]; then git -C /home/claude/wt159 show origin/main:tests/test_sql.c > tests/test_sql.c; fi
python3 - $plant <<'PY'
import sys
k=int(sys.argv[1])
if k:
    line=[l for l in open('tests/plants/sql.txt') if l.startswith('sql:') and ('3980' in l if k==2 else 'firstdone' in l)]
    assert len(line)==1
    name,target,prep,file,old,new=line[0].rstrip('\n').split('~')
    s=open(file).read(); assert s.count(old)==1
    open(file,'w').write(s.replace(old,new)); print('planted:',name)
PY
cmd=$(cat /tmp/claude-0/rv/cmd.txt)
eval "${cmd/-o build\/san\/test_sql/-o /tmp/claude-0/rv/$name}"
if [ -z "$base" ]; then
  printf '#define main real_main\n#include "test_sql.c"\n#undef main\nint main(void){ (void)real_main; cancel_tokens(); puts("iso ok"); return 0; }\n' > tests/iso.c
  c2=${cmd/tests\/test_sql.c/tests\/iso.c}
  eval "${c2/-o build\/san\/test_sql/-o /tmp/claude-0/rv/$name.iso}"
fi
echo built $name
