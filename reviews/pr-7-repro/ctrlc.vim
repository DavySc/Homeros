set noswapfile
call timer_start(100, {-> nvim_input(g:keys)})
call timer_start(400, {-> nvim_input(g:keys2)})
call timer_start(900, {-> execute('silent w! ' . g:out . ' | qa!')})
