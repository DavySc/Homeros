" Is '' (Neovim's w_pcmark) always an entry of the jump list that Win.jump[]
" could give?  Prints '' and the jump list after some key sequences.
"   nvim --headless -u NONE -i NONE -n -S reviews/pr-11-repro/pcmark.vim
func! s:Show(keys)
  enew! | call setline(1, map(range(1, 20), '"line " . v:val')) | clearjumps
  exe "normal! " . a:keys
  let [l, i] = getjumplist()
  call writefile([printf("%-28s cursor %2d  '' %2d  jump list %s at %d", strtrans(a:keys), line('.'), getpos("''")[1], string(map(l, 'v:val.lnum')), i)], '/dev/stdout', 'a')
endfunc
call s:Show("5G")
call s:Show("5G10G15G")
call s:Show("5G10G15G\<C-o>")
call s:Show("5G10G15G\<C-o>\<C-o>")
call s:Show("5G10G15G\<C-o>\<C-o>\<Tab>")
call s:Show("5G10G15G\<C-o>\<C-o>''")
call s:Show("5G10G15G''''")
call s:Show("5G10G15G\<C-o>\<C-o>12G")
call s:Show("5G10G15G\<C-o>\<C-o>\<C-o>''")
call s:Show("5G10G5G''")
qa!
