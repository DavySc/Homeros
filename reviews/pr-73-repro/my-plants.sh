# my-plants.sh REPO: only my-plants.txt, with REPO's planted.sh in a scratch copy
p=$(dirname "$(readlink -f "$0")")/my-plants.txt
t=$(mktemp -d); cd "$1" && cp -r Makefile assets include ref src tests tools design "$t"/
rm "$t"/tests/plants/*.txt; cp "$p" "$t/tests/plants/"
cd "$t" && bash tools/planted.sh; rm -rf "$t"
