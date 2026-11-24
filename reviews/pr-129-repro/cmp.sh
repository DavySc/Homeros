#!/bin/bash
# cmp.sh EXT step...: the buffer's first line after the steps in a fresh file: the PR, main and nvim
export PATH=/home/claude/Homeros/tools-env/bin:$PATH
ext=$1; shift
one() { d=$(mktemp -d); printf 'one alpha beta gamma\nsecond line here\n' > $d/f.$ext
  HOME=$d XDG_STATE_HOME=$d/s XDG_DATA_HOME=$d/d XDG_CACHE_HOME=$d/c python3 drv.py 10x60 "$1 $d/f.$ext" '~0.5' "${@:2}" '~0.3' '!' | sed -n 2p | sed -E 's/^ 0\|([0-9]+ +)?//'; rm -rf $d; }
p=$(one "/home/claude/wt/homeros -t" "$@"); m=$(one "/home/claude/wtmain/homeros -t" "$@"); n=$(one "nvim --clean -n" "$@")
printf '.%s %-34s PR [%s] main [%s] nvim [%s]%s\n' $ext "$*" "$p" "$m" "$n" "$([ "$p" = "$n" ] || echo '  PR DIFFERS')"
