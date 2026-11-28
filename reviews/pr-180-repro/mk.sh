# usage: mk.sh TREE OUTDIR: TREE's sources as a sanitizer archive and an -O2 one; OUTDIR/vars has FLAGS and LINK
T=$1; O=$2; cd $T
L=$(make -n test TESTS=zz 2>/dev/null | grep -- "-o build/san/test_\$t" | sed "s/ || exit 1.*//")
FLAGS=$(echo "$L" | sed 's/^[ \t]*gcc //; s/ -o build\/san\/test_.t.*//')
REST=$(echo "$L" | sed 's/.*tests\/test_.t\.c //')
SRCS=$(for x in $REST; do case $x in src/*.c|ref/*.c) echo $x;; esac; done)
LINK=$(for x in $REST; do case $x in src/*.c|ref/*.c) ;; build/*) echo $T/$x;; *) echo $x;; esac; done | tr '\n' ' ')
OPT=$(echo "$FLAGS" | sed 's/-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all/-O2/')
mkdir -p $O/san $O/opt
printf 'FLAGS="%s"\nOPT="%s"\nLINK="%s"\n' "$FLAGS" "$OPT" "$LINK" > $O/vars
for s in $SRCS; do n=$(echo $s | tr / _); echo "gcc $FLAGS -c -o $O/san/$n.o $s && gcc $OPT -c -o $O/opt/$n.o $s"; done > $O/cmds
xargs -P 3 -I{} sh -c "{}" < $O/cmds || exit 1
rm -f $O/san.a $O/opt.a; ar rcs $O/san.a $O/san/*.o; ar rcs $O/opt.a $O/opt/*.o; echo built $O
