cd $S
for v in 15 16 17; do for m in 0 1 2 3 4 5 6; do
  find -L zig$v/lib/zig/std -name '*.zig' | sort | xargs repro/fmtcmp $v $S/zig$v/bin/zig $S/tmp$v $m 2>> $S/fmt-$v.err >> $S/fmt.out
done; done
# a release's formatter on another release's std: what a wrong pick does
for pair in "17 16" "17 15" "15 16" "16 17" "15 17" "16 15"; do set -- $pair
  find -L zig$2/lib/zig/std -name '*.zig' | sort | xargs repro/fmtcmp $1 $S/zig$1/bin/zig $S/tmp$1 0 2>> $S/fmt-cross.err | sed "s/$/ (on std 0.$2)/" >> $S/fmt.out
done
echo done > $S/fmt.done
