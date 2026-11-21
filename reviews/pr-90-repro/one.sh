# one.sh TEXTFILE 'keys with \r \e': ours and Neovim (through the PR's ex_oracle.lua) side by side
d=$(mktemp -d); printf "$2\n" > $d/cases
arg=$(printf "$2" | od -An -tx1 -v | tr -d '\n' | sed 's/ /\\x/g')
echo "ours:"; XDG_STATE_HOME=$d/st build/probe-san "$1" "$arg" | sed 's/ keys .*//'
timeout 10 nvim --headless --clean -l src/keys/ex_oracle.lua "$1" $d/cases $d/out 1 2>/dev/null
echo "nvim:"; head -2 $d/out | tr '\001' '\n' | sed '2,$s/.*/|&|/'
rm -rf $d
