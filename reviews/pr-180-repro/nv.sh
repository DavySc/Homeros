#!/bin/sh
# usage: nv.sh PROBE  (Neovim 0.12 in an 80x24 tmux with Davy's render-markdown settings)
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp)
tmux -L nv$$ new-session -d -x 80 -y 24 "FAKE=$here/fake.py PROBE=$1 OUT=$out RM_RTP=$RM_RTP nvim --clean -c 'luafile $INIT' -c 'luafile $here/nv.lua'"
while tmux -L nv$$ has-session 2>/dev/null; do sleep 0.2; done
cat $out
