# usage: seeds.sh VARIANT FROM TO ; per seed: nvim ">-1" vs "-1" traces equal, and the module replays the ">-1" trace
v=$1; bad=0; nY=0; nR=0
for s in $(seq $2 $3); do
  nvim --headless --clean -l $v.lua 100 $s '' inc > t_$v.txt 2>/dev/null
  nvim --headless --clean -l ${v}_asminus.lua 100 $s '' inc > m_$v.txt 2>/dev/null
  cmp -s t_$v.txt m_$v.txt || { echo "NVIM DIFF $v seed $s"; bad=$((bad+1)); }
  ./tf t_$v.txt >/dev/null 2>e_$v.txt || { echo "MODULE DIFF $v seed $s: $(head -1 e_$v.txt)"; bad=$((bad+1)); }
  nY=$((nY + $(grep -v '^R' t_$v.txt | grep -o ' Y' | wc -l))); nR=$((nR + $(grep -c '^R' t_$v.txt)))
done
echo "$v seeds $2-$3: bad=$bad Ytokens=$nY results=$nR"
