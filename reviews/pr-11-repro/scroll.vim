" core.h: "core_scroll, core_scroll_half and core_scroll_page return -1 when
" they could not move (Neovim beeps, which ends a macro or mapping)".
" A beep ends :normal, so an x after the key runs only if the key did not beep.
"   nvim --headless -u NONE -i NONE -n -S reviews/pr-11-repro/scroll.vim
func! s:Try(name, start, key)
  enew! | call setline(1, map(range(1, 100), '"line " . v:val'))
  exe "normal! " . a:start
  let [top, cur] = [line('w0'), line('.')]
  exe "normal! " . a:key . "x"
  let moved = line('w0') != top || line('.') != cur
  let beeped = getline('.') =~ '^line'
  call writefile([printf("%-28s %s, %s", a:name, moved ? 'moved' : 'could not move', beeped ? 'beep (x did not run)' : 'no beep (x ran)')], '/dev/stdout', 'a')
endfunc
call s:Try("CTRL-E in the middle",  "gg", "\<C-e>")
call s:Try("CTRL-E, last line on top", "Gzt", "\<C-e>")
call s:Try("CTRL-Y in the middle",  "50Gzt", "\<C-y>")
call s:Try("CTRL-Y at the top",     "gg", "\<C-y>")
call s:Try("CTRL-D in the middle",  "gg", "\<C-d>")
call s:Try("CTRL-D on the last line", "G", "\<C-d>")
call s:Try("CTRL-U in the middle",  "50G", "\<C-u>")
call s:Try("CTRL-U on line 1",      "gg", "\<C-u>")
call s:Try("CTRL-F in the middle",  "gg", "\<C-f>")
call s:Try("CTRL-F on the last line", "G", "\<C-f>")
call s:Try("CTRL-F, last line on top", "Gzt", "\<C-f>")
call s:Try("CTRL-E with a count past the end", "90Gzt", "50\<C-e>")
call s:Try("CTRL-Y with a count past the top", "5Gzt", "50\<C-y>")
call s:Try("CTRL-B in the middle",  "50G", "\<C-b>")
call s:Try("CTRL-B on line 1",      "gg", "\<C-b>")
qa!
