" What core.h's Mark comment says of Neovim, asked of Neovim itself:
"   nvim --headless -u NONE -i NONE -n -S reviews/pr-11-repro/marks.vim
" Prints one line per claim; the last line counts the wrong ones.
let s:bad = 0
func! s:Is(what, got, want)
  let ok = a:got ==# a:want
  if !ok | let s:bad += 1 | endif
  call writefile([(ok ? 'ok     ' : 'WRONG  ') . a:what . ': ' . string(a:got) . (ok ? '' : ' (header says ' . string(a:want) . ')')], '/dev/stdout', 'a')
endfunc
func! s:Fresh()
  enew!
  call setline(1, map(range(1, 10), '"line " . v:val . " abc def"'))
  clearjumps
endfunc

" a-z on a deleted line: unset
call s:Fresh() | exe "normal! 3Gma" | 2,5d
call s:Is("'a after its line is deleted", getpos("'a")[1:2], [0, 0])
" a-z below deleted lines: moves up
call s:Fresh() | exe "normal! 8G4|mb" | 2,5d
call s:Is("'b below 4 deleted lines", getpos("'b")[1:2], [4, 4])
" ^ on a deleted line: unset
call s:Fresh() | exe "normal! 3Gix\<Esc>" | 2,5d
call s:Is("'^ after its line is deleted", getpos("'^")[1:2], [0, 0])
" < and > on deleted lines: the first deleted line
call s:Fresh() | exe "normal! 3GVj\<Esc>" | 2,5d
call s:Is("'< after its line is deleted", getpos("'<")[1], 2)
call s:Is("'> after its line is deleted", getpos("'>")[1], 2)
" the jump list on deleted lines: the first deleted line (:d from line 1
" moves the entry for line 1 to the end, so compare in line order)
call s:Fresh() | exe "normal! 6GGgg"
let s:was = map(getjumplist()[0], 'v:val.lnum')
5,7d
call s:Is("jump list " . string(s:was) . " after 5,7d", sort(map(getjumplist()[0], 'v:val.lnum'), 'n'), map(s:was, 'v:val < 5 ? v:val : v:val <= 7 ? 5 : v:val - 3'))
" the change list on deleted lines: the first deleted line (the delete adds
" its own entry at the end; let &ul = &ul closes the undo block, without
" which a script's changes share one entry)
call s:Fresh() | exe "normal! 3Gx" | let &ul = &ul | exe "normal! 6Gx" | let &ul = &ul | exe "normal! 9Gx" | let &ul = &ul
let s:was = map(getchangelist()[0], 'v:val.lnum')
5,7d
call s:Is("change list " . string(s:was) . " after 5,7d", map(getchangelist()[0], 'v:val.lnum')[:len(s:was) - 1], map(s:was, 'v:val < 5 ? v:val : v:val <= 7 ? 5 : v:val - 3'))
" [ and ] are not moved by lines deleted above (the delete itself sets them)
call s:Fresh() | exe "normal! 8Gyy" | 2,3d
call s:Is("'[ after yy on 8 then 2,3d", getpos("'[")[1], 2)
" a join moves a mark onto the joined line (mark_col_adjust)
call s:Fresh() | exe "normal! 2G6|mc1GJ"
call s:Is("'c at 2,6 after 1GJ", getpos("'c")[1:2], [1, len("line 1 abc def") + 1 + 6])
" 100 entries in both lists
call s:Fresh() | for i in range(1, 150) | exe "normal! " . (i % 10 + 1) . "G" . (i % 9 + 1) . "G" | endfor
call s:Is("jump list length is at most", len(getjumplist()[0]) <= 100, v:true)
call writefile([s:bad . ' wrong'], '/dev/stdout', 'a')
qa!
