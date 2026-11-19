#!/usr/bin/env bash
# Finding 1 in real Neovim: a terminal shrunk below what the windows need,
# then 17 CTRL-W >.  Prints each window's [screenpos, width, height] before
# the shrink, after it, and after the resize command.  Needs tmux and nvim.
d=$(mktemp -d); cd "$d"
printf 'ab \xce\xbbx \xe1\x83\x90\xf0\x90\x90\x80\n\tef\n\n  gh\n' > f0
SETUP='set noswapfile nomore shortmess+=I number relativenumber scrolloff=6 splitright splitbelow nowrap'
W=":call writefile([string(map(range(1,winnr('\$')),'[win_screenpos(v:val),winwidth(v:val),winheight(v:val)]')).' lines '.&lines], '$d/out', 'a')"
tmux new-session -d -s bent -x 40 -y 12 "nvim --clean --cmd '$SETUP' f0"; sleep 0.8
for k in ':3vs' ':3sp' ':10vs' ':new'; do tmux send-keys -t bent "$k" Enter; sleep 0.2; done
tmux send-keys -t bent "$W" Enter; sleep 0.3
tmux resize-window -t bent -x 15 -y 4; sleep 0.5
tmux send-keys -t bent "$W" Enter; sleep 0.3; tmux send-keys -t bent Enter
tmux send-keys -t bent '17' C-w '>'; sleep 0.3
tmux send-keys -t bent "$W" Enter; sleep 0.3
cat out; tmux kill-session -t bent
