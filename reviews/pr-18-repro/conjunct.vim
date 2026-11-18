" DESIGN.md (PR 18): "Neovim's utf_head_off() can stop inside a cluster its
" forward step made (a Devanagari conjunct)".  Byte columns after l from the
" start and h from the end of "a" + KA VIRAMA SSA + "b":
"   nvim --headless -u NONE -i NONE -n -S reviews/pr-18-repro/conjunct.vim
call setline(1, "aक्षb")
let fwd = [] | normal! 0
for i in range(4) | call add(fwd, col('.')) | normal! l
endfor
let back = [] | normal! $
for i in range(4) | call add(back, col('.')) | normal! h
endfor
call writefile(['l from the start stops at byte columns ' . string(uniq(fwd)), 'h from the end stops at byte columns   ' . string(uniq(back))], '/dev/stdout', 'a')
qa!
