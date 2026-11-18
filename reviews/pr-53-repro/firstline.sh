#!/usr/bin/env bash
# Review of PR 53, finding 1: Neovim decides 'fileformat' from the first
# chunk it reads; a first line longer than that chunk makes a CR LF file
# unix in Neovim (^M shown), dos here.  Prints nvim's and the driver's
# format for a first line of N bytes followed by CR LF lines.
# usage: firstline.sh DRIVER N...
drv=$1; shift
d=$(mktemp -d)
for n in "$@"; do
	python3 -c "import sys; sys.stdout.buffer.write(b'a' * $n + b'\r\nb\r\nc\r\n')" > $d/f
	nv=$(nvim --headless --clean -n -c 'set nofixeol' -c 'lua io.stdout:write(vim.bo.fileformat .. " " .. #vim.api.nvim_buf_get_lines(0,0,1,true)[1] .. " " .. vim.fn.strtrans(vim.api.nvim_buf_get_lines(0,1,2,true)[1]))' -c 'silent w' -c q $d/f 2>&1)
	cmp -s $d/f <(python3 -c "import sys; sys.stdout.buffer.write(b'a' * $n + b'\r\nb\r\nc\r\n')") && same=same || same=CHANGED
	ours=$(XDG_STATE_HOME=$d/state $drv $d/f nowrite </dev/null | sed -n 's/^open ff=\([a-z]*\).*/\1/p')
	echo "first line $n bytes: nvim '$nv' (file after :w $same), ours $ours"
done
rm -rf $d
