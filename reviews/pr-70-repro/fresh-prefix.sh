# Finding 4: test_git_win on a Wine prefix that does not exist yet, then on
# the same prefix again.  From the repo root, after src/git/check.sh built
# build/win/test_git_win.exe.  With an argument: wineboot first.
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-$PWD/build/win/run} WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=$PWD/build/win/prefix-git LANG=C.UTF-8
export XDG_DATA_HOME=$PWD/build/win/git-data TEST_TRASH=$PWD/build/win/git-data/Trash/files
wineserver -k 2>/dev/null; rm -rf build/win/prefix-git "$XDG_DATA_HOME"
[ -z "${1:-}" ] || { wine wineboot -i >/dev/null 2>&1; wineserver -w; }
echo "fresh prefix:"; timeout 200 wine build/win/test_git_win.exe 2>&1 | cut -c1-110 | tail -1
rm -rf "$XDG_DATA_HOME"
echo "the same prefix again:"; timeout 200 wine build/win/test_git_win.exe 2>&1 | cut -c1-110 | tail -1
