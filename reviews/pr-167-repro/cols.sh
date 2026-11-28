# cols.sh PROBE FILE TABLE...: the popup's columns after "d." for each table
p=$1; f=$2; shift 2
for t in "$@"; do (cat $f; printf '\nselect * from zz q join %s d on d|\n' $t) > /tmp/_x.sql; echo "$t: $(MAXI=99 $p /tmp/_x.sql p.sql . 2>&1 | head -1)"; done
