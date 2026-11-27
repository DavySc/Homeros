# usage: seeds.sh LUA FROM TO [inc]
bad=0; steps=0
for s in $(seq $2 $3); do
  nvim --headless --clean -l $1 300 $s '' $4 > t.txt 2>/dev/null
  steps=$((steps + $(grep -c '^R' t.txt)))
  ./tf t.txt >/dev/null 2>e.txt || { bad=$((bad+1)); echo "seed $s: $(head -1 e.txt)"; cp t.txt fail-$s.txt; }
done
echo "$1 $2-$3 $4: $bad seeds differ, $steps results; levels: $(grep '^R' t.txt | tr ' ' '\n' | awk -F/ 'NF==4{c[$4]++} END{for(k in c) printf "%s:%d ", k, c[k]}')"
