#!/usr/bin/env bash
# battery.sh VARIANT N LABEL [JOBS]: N runs of the variant's probe, JOBS at a
# time; every ROW line and each run's exit status into out/VARIANT.LABEL
R=$(dirname "$(readlink -f "$0")"); v=$1 n=$2 label=$3 jobs=${4:-1}
mkdir -p $R/out; o=$R/out/$v.$label; : > $o
cd $R/v/$v
seq $n | xargs -P $jobs -I{} sh -c './build/san/test_probe > /tmp/p.$$ 2>/tmp/e.$$; rc=$?; { grep ROW /tmp/p.$$ | sed "s/^/{} /"; echo "RC {} $rc"; grep TRY /tmp/p.$$ | sed "s/^/{} /"; } > /tmp/o.$$; flock '$o' cat /tmp/o.$$ >> '$o'; rm -f /tmp/[peo].$$'
