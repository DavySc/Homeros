#!/usr/bin/env bash
# my-planted.sh REPO: runs my-plants.txt with REPO's tools/planted.sh, in a scratch copy whose plants are only these.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
t=$(mktemp -d)
cp -r "$1"/Makefile "$1"/assets "$1"/include "$1"/ref "$1"/src "$1"/tests "$1"/tools "$1"/design "$t"/
mkdir -p "$t/build" && cp -r "$1"/build/syntax "$t/build/" 2>/dev/null || true
rm "$t"/tests/plants/*.txt
cp "$here/my-plants.txt" "$t/tests/plants/"
bash "$t/tools/planted.sh"
rm -rf "$t"
