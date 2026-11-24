call setline(1,["abc def","ghi jkl","","x"])
let v:errmsg = ''
try
  exe "'"
catch
  echo 'caught' v:exception
endtry
echo 'errmsg[' . v:errmsg . ']'
normal! jm<
let v:errmsg = ''
normal! gv
echo mode() line('.') 'errmsg[' . v:errmsg . ']'
qa!
