# usage: mut.sh NAME 'sed-expr' [trace...] ; builds fold.c with the mutation, replays traces
R=/home/claude/r154b; name=$1; ex=$2; shift 2
sed "$ex" $R/src/fold/fold.c > m/fold.c
cmp -s m/fold.c $R/src/fold/fold.c && { echo "$name: MUTATION DID NOT APPLY"; exit; }
gcc -O1 -g -fsanitize=address,undefined -I$R/include -I$R/tests $R/tests/test_fold.c m/fold.c -o m/tf 2>m/err || { echo "$name: build failed"; exit; }
for t in "$@"; do ./m/tf $t >/dev/null 2>&1 && r=survives || r=caught; printf "%s on %s: %s\n" "$name" "$(basename $t)" $r; done
