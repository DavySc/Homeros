" What Neovim's jump list holds for a buffer after :bd, which Win.jump[]'s
" Buf pointer has to follow.
"   nvim --headless -u NONE -i NONE -n -S reviews/pr-11-repro/bd.vim
func! s:Say(what)
  call writefile([printf("%-22s %s", a:what, string(map(getjumplist()[0], '[v:val.bufnr, v:val.lnum]')))], '/dev/stdout', 'a')
endfunc
call writefile(map(range(1, 20), '"a " . v:val'), '/tmp/pr11-a.txt')
call writefile(map(range(1, 20), '"b " . v:val'), '/tmp/pr11-b.txt')
edit /tmp/pr11-a.txt | clearjumps
exe "normal! 5G10G"
edit /tmp/pr11-b.txt
exe "normal! 7G"
call s:Say("before :bd 1")
bdelete 1
call s:Say("after :bd 1")
exe "normal! \<C-o>\<C-o>\<C-o>"
call writefile(["after 3 CTRL-O: buffer " . bufnr('') . " line " . line('.') . ", jumpoptions=" . &jumpoptions], '/dev/stdout', 'a')
qa!
