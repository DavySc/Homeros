# usage: probe.sh OUTDIR(of mk.sh) TREE file.c [san|opt] -> builds OUTDIR/<name>
O=$1; T=$2; f=$3; k=${4:-san}; . $O/vars; n=$(basename $f .c)
[ $k = san ] && F="$FLAGS" || F="$OPT"
(cd $T && gcc $F -I$T/tests -I$(dirname $f) -o $O/$n-$k $f $O/$k.a $LINK)
