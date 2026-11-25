#!/bin/bash
# drv.sh <org.c> <out>: the Org driver at -O2 with that org.c
R=/home/claude/Homeros; t=$(mktemp -d); mkdir -p $t/src/org; cp $R/src/org/*.h $t/src/org/; cp "$1" $t/src/org/org.c
gcc -std=c17 -D_DEFAULT_SOURCE -O2 -I$R/include -o "$2" $R/tests/org_drv.c $t/src/org/org.c $R/src/text/*.c $R/src/uni/*.c 2>&1 | grep -E "error" ; rm -rf $t
