#!/bin/sh
# Neovim typed into through tmux (80x24): 3G4l K, screen; l h K, screen
here=$(cd "$(dirname "$0")" && pwd); S=nvk$$
tmux -L $S new-session -d -x 80 -y 24 "PRE=$PRE FAKE=$here/fake.py RM_RTP=$RM_RTP nvim --clean -c 'luafile $INIT' -c 'luafile $here/nvk.lua'"
sleep 3; tmux -L $S send-keys -l '3G04lK'; sleep 3; echo "--- first K"; tmux -L $S capture-pane -p | head -16
tmux -L $S send-keys -l 'lh'; sleep 1; tmux -L $S send-keys -l 'K'; sleep 3; echo "--- second K"; tmux -L $S capture-pane -p | head -16
tmux -L $S kill-server
