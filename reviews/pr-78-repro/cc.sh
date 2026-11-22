# usage: cc.sh probe.c out [extra flags]; build like the Makefile's tests (ASan+UBSan)
cd "${ROOT:-.}"
src=$1 out=$2; shift 2
eval "$(make --no-print-directory --eval 'pv:;@echo "F=\"$(WARN) $(CPPFLAGS) $(SAN)\"; S=\"$(SRCS)\"; O=\"$(OBJS) $(LIBS)\""' pv)"
S=$(echo $S | tr ' ' '\n' | grep -v 'src/main.c' | tr '\n' ' ')
gcc $F "$@" -Itests -o $out $src $S $O
