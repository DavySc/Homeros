#!/usr/bin/env bash
# Review of PR 53: runs wiring.c's two checks, and the reload in nvim beside it.
# usage: wiring.sh WIRING_BINARY   (from the repo root)
w=$1; d=$(mktemp -d); export XDG_STATE_HOME=$d/state
for pair in 'aNbN|pRNqRN' 'aRNbRN|pNqN'; do
	echo "== file ${pair%|*} replaced by ${pair#*|}"
	$w reload $d/f "${pair%|*}" "${pair#*|}"
	echo "   ours on disk: $(od -An -c $d/f | tr -s ' ')"
	printf '%s' "${pair%|*}" | sed 's/R/\r/g' | tr N '\n' > $d/n
	cat > $d/s.lua <<L
vim.o.fixendofline = false
vim.cmd('edit $d/n'); vim.cmd('normal ix'); vim.cmd('silent write')
local f = io.open('$d/n', 'wb'); f:write((('${pair#*|}'):gsub('R', '\r'):gsub('N', '\n'))); f:close()
vim.cmd('silent edit!')
local function s(t) io.stdout:write(t .. ' ff=' .. vim.bo.ff .. ' ' .. vim.inspect(vim.api.nvim_buf_get_lines(0, 0, -1, true)):gsub('\n', '') .. '\n') end
s('   nvim after :e!'); vim.cmd('silent undo'); s('   nvim after u'); vim.cmd('silent write!')
vim.cmd('silent redo'); s('   nvim after CTRL-R')
L
	nvim --headless --clean -n -l $d/s.lua 2>&1
	echo "   nvim on disk after u, :w!: $(od -An -c $d/n | tr -s ' ')"
done
echo "== syntax faces, LF copy against CR LF copy"
for f in src/core/core.c src/syntax/release.c tools/org_dump.lua README.md; do
	[ -e $f ] || continue
	x=${f##*.}; cp $f $d/lf.$x; sed 's/$/\r/' $f > $d/crlf.$x
	$w syntax $d/lf.$x $d/crlf.$x | tail -1 | sed "s|^|$f: |"
done
rm -rf $d
