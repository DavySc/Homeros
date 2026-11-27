call writefile(["a1","a2","a3","a4"], "/tmp/tbA")
call writefile(["b1","b2","b3","b4"], "/tmp/tbB")
edit /tmp/tbA
normal! jjyy
edit /tmp/tbB
let v:errmsg = ''
silent! normal! `[
echo 'in B:' line('.') v:errmsg
normal! ggx
edit! /tmp/tbA
let v:errmsg = ''
silent! normal! G`[
echo 'back in A:' line('.') v:errmsg
qa!
