# ctrl-e.sh: CTRL-E while :s///c asks, with the window showing the file's end
# (60 lines "N foo", 22-line window, cursor on line 45 = 'scrolloff' rows from
# the top).  Neovim's top line, recorded by the replacement expression at "y":
f=$(mktemp); seq 1 60 | sed 's/$/ foo/' > $f; d=$(mktemp -d)
(cd $d && nvim --headless --clean -n -c 'set scrolloff=6 nowrap' \
  -c "call feedkeys(\"G:45,60s/foo/\\\\=execute(\\\"let g:w=line('w0')\\\")/c\\<CR>\\<C-e>\\<C-e>\\<C-e>yq\", 'tx')" \
  -c 'call writefile(["Neovim top " . g:w], "out")' -c 'qa!' $f >/dev/null 2>&1; cat out)
# ours: a probe.c that also prints e.win->top + 1, fed  G:45,60s/foo/x/c\r\x05\x05\x05
rm -rf $d $f
