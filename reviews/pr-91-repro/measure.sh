cd /home/claude/probe
for i in 1 2 3 4 5; do
  echo "run $i"
  echo "pr   all.nix 6000:   $(./bench d/all.nix 6000 100)"
  echo "pr   big.c 6000:     $(./bench d/big.c 6000 100)"
  echo "main big.c 6000:     $(./bench_main d/big.c 6000 100)"
  echo "pr   big3.rs 6000:   $(./bench d/big3.rs 6000 100)"
  echo "main big3.rs 6000:   $(./bench_main d/big3.rs 6000 100)"
  echo "pr   wide.lua 6000:  $(./bench d/wide.lua 6000 100)"
  echo "main wide.lua 6000:  $(./bench_main d/wide.lua 6000 100)"
  echo "pr   design.md 150:  $(./bench d/design.md 150 100)"
  echo "main design.md 150:  $(./bench_main d/design.md 150 100)"
  echo "pr   lualong250 100: $(./bench d/lualong250.nix 100 50)"
  echo "pr   luareal.nix 100:$(./bench d/luareal.nix 100 50)"
  echo "pr   luareal.lua 100:$(./bench d/luareal.lua 100 50)"
  echo "main souffle: $(./ws_main 2 1000000)"
  echo "pr   souffle: $(./ws_pr91b 2 1000000)"
  echo "pr   tsql:    $(./ws_pr91b 0 1000000)"
done
uptime
