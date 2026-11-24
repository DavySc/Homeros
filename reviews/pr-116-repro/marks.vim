call setline(1,["a漢x","b漢","c"])
func! T(k)
  exe "normal! gg0" . a:k
  echo a:k getpos("'[")[1:2] getpos("']")[1:2]
endfunc
call T("yl")
call T("lyl")
call T("yw")
call T("jjy0")
call T("jjyk")
call T("y0")
call T("lvy")
call T("3~")
call T("l~")
call T("rz")
call T("2rz")
call T("jlrz")
qa!
