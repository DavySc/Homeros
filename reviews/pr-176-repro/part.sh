# Every plant position must be picked by exactly one shard named in the
# workflow file $1 (same selection as tools/planted.sh's last line).
# Run from the repo root: bash reviews/pr-176-repro/part.sh .github/workflows/check.yml
wf=$1; pos=$(mktemp)
cat tests/plants/*.txt | grep -v -e "^#" -e "^$" | awk '{print NR}' > "$pos"
for s in $(grep -o 'planted SHARD=[0-9]*/[0-9]*' "$wf" | sed 's/.*=//'); do
	[[ $s =~ ^([0-9]+)/([1-9][0-9]*)$ ]] && (( BASH_REMATCH[1] < BASH_REMATCH[2] )) || { echo "REJECTED $s"; continue; }
	awk -v k="${s%/*}" -v n="${s#*/}" '(NR - 1) % n == k' "$pos"
done | sort -n | uniq -c | awk -v all="$(wc -l < "$pos")" '$1 != 1 { bad++ } END { print NR " of " all " positions picked, " bad + 0 " not exactly once"; exit !(NR == all && !bad) }'
