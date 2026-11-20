# ctrl-e.sh: CTRL-E while :s///c asks, with the window showing the file's end
# (60 lines "N foo", 22-line window, cursor on line 45 = 'scrolloff' rows from
# the top).  Prints Neovim's top line, recorded by the replacement expression
# at "y".  Ours: a probe.c that also prints e.win->top + 1, fed
#   G:45,60s/foo/x/c\r\x05\x05\x05
d=$(mktemp -d); seq 1 60 | sed 's/$/ foo/' > $d/t
cat > $d/s.vim <<'V'
set scrolloff=6 nowrap
call feedkeys("G:45,60s/foo/\\=execute('let g:w=line(\"w0\")')/c\<CR>\<C-e>\<C-e>\<C-e>yq", 'tx')
call writefile(["Neovim top " . g:w], "out")
qa!
V
(cd $d && nvim --headless --clean -n -S s.vim t >/dev/null 2>&1; cat out); rm -rf $d
