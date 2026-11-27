#!/usr/bin/env bash
# rr.sh ROUNDS LABEL VARIANT...: round by round, one probe run of each
# variant, appended to out/VARIANT.LABEL (so all see the same weather)
R=$(dirname "$(readlink -f "$0")"); n=$1 label=$2; shift 2
for i in $(seq $n); do for v in "$@"; do
  (cd $R/v/$v; ./build/san/test_probe > /tmp/rr.p 2>/dev/null; rc=$?; { grep ROW /tmp/rr.p | sed "s/^/$i /"; echo "RC $i $rc"; grep TRY /tmp/rr.p | sed "s/^/$i /"; } >> $R/out/$v.$label)
done; done
