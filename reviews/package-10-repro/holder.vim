" Neovim's jobstart on the same commands as holder.c: when does on_exit come
" when something the child started still holds its stdout?
" nvim --headless -n -u NONE -S holder.vim
let s:t0 = reltime()
function! s:Ms() abort
  return float2nr(reltimefloat(reltime(s:t0)) * 1000)
endfunction
function! s:Ev(name, id, data, ev) abort
  call add(s:log, printf('%5d ms  %s %s %s', s:Ms(), a:name, a:ev, string(a:data)))
  if a:ev ==# 'exit' | let s:done = 1 | endif
endfunction
for s:cmd in ['sleep 3 & echo hi', 'setsid sleep 3 & echo hi']
  let s:log = [] | let s:done = 0 | let s:t0 = reltime()
  call jobstart(['sh', '-c', s:cmd], {
        \ 'on_stdout': function('s:Ev', ['job']), 'on_stderr': function('s:Ev', ['job']), 'on_exit': function('s:Ev', ['job'])})
  call wait(10000, {-> s:done})
  call writefile([s:cmd] + s:log, '/dev/stdout', 'a')
endfor
qa!
