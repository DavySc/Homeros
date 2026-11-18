# The undo file's system calls differ on Windows (fsync, rename) and long is
# 32 bits there: src/undo/win.sh compiles this package for Windows with our
# warnings and runs tests/test_undo_win.c under Wine, so a break shows here
# and not in the Windows release.  Part of compilers, not of make win, whose
# plants do not count a compile error as a catch (tools/planted.sh).
compilers: undo-win
undo-win:
	@bash src/undo/win.sh
.PHONY: undo-win
