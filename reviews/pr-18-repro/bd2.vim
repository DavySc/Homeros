" core.h (PR 18): ":bd forgets the buffer's entries in the current window
" only (do_buffer_ext); other windows may keep a buffer :bd deleted, which
" stays allocated (unlisted)".  Asked of Neovim:
"   nvim --headless -u NONE -i NONE -n -S reviews/pr-18-repro/bd2.vim
func! s:Say(what)
  let l = []
  for w in range(1, winnr('$'))
    call add(l, 'window ' . w . ' ' . string(map(getjumplist(w)[0], '[v:val.bufnr, v:val.lnum]')))
  endfor
  call writefile([printf("%-16s %s", a:what, join(l, '   '))], '/dev/stdout', 'a')
endfunc
call writefile(map(range(1, 20), '"a " . v:val'), '/tmp/pr18-a.txt')
call writefile(map(range(1, 20), '"b " . v:val'), '/tmp/pr18-b.txt')
edit /tmp/pr18-a.txt | clearjumps
exe "normal! 5G10G"
split
exe "normal! 15G"
edit /tmp/pr18-b.txt
exe "normal! 7G"
wincmd j
edit /tmp/pr18-b.txt
exe "normal! 3G"
wincmd k
call s:Say("before :bd 1")
bdelete 1
call s:Say("after :bd 1")
call writefile(["buffer 1 exists " . bufexists(1) . ", listed " . buflisted(1)], '/dev/stdout', 'a')
wincmd j
exe "normal! \<C-o>\<C-o>\<C-o>"
call writefile(["3 CTRL-O in the other window: buffer " . bufnr('') . " line " . line('.') . ", listed " . buflisted(bufnr(''))], '/dev/stdout', 'a')
call s:Say("after that")
qa!
