#!/bin/sh
# Puts VS Code's infoview page where the editor's \w looks for it
# ($HOMEROS_INFOVIEW, else stdpath("data")/infoview: $XDG_DATA_HOME/homeros,
# else ~/.local/share/homeros; on Windows, from Git Bash, %LOCALAPPDATA%\homeros-data,
# as file_stdpath): the npm package @leanprover/infoview
# (vscode-lean4's, the version kittymax ships) in dist/, and this
# directory's page/ (the host page, its script and the theme) beside it.
# Run it again after pulling a change to page/.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}/homeros
[ "${OS:-}" = Windows_NT ] && data=${XDG_DATA_HOME:-$LOCALAPPDATA}/homeros-data
root=${HOMEROS_INFOVIEW:-$data/infoview}
version=0.14.0
sum=c71ea9994e86ae2cfbf62db38cd1822a5b9c8d967d8bfa29b079982dc4dec8b3   # sha256 of npm's tarball
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL -o "$tmp/p.tgz" "https://registry.npmjs.org/@leanprover/infoview/-/infoview-$version.tgz"
echo "$sum  $tmp/p.tgz" | sha256sum -c - >/dev/null
tar -xzf "$tmp/p.tgz" -C "$tmp"
mkdir -p "$root"
rm -rf "$root/dist"
mv "$tmp/package/dist" "$root/dist"
cp "$here/page/index.html" "$here/page/host.js" "$here/page/theme.css" "$root/"
echo "infoview $version in $root"
