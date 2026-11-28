#!/bin/sh
here=$(cd "$(dirname "$0")" && pwd); out=$(mktemp)
tmux -L nv$$ new-session -d -x 80 -y 24 "FAKE=$here/fake.py CLOSE=$1 OUT=$out RM_RTP=$RM_RTP nvim --clean -c 'luafile $INIT' -c 'luafile $here/nv4.lua'"
while tmux -L nv$$ has-session 2>/dev/null; do sleep 0.2; done
cat $out
