# The undo file's system calls differ on Windows (fsync, rename) and long is
# 32 bits there: the Windows build's compiler (zig cc, as src/win/check.sh)
# compiles this package with our warnings, and tests/test_undo_win.c runs
# under Wine, so a break shows here and not in the Windows release.
compilers: undo-win
undo-win:
	@mkdir -p build/win
	@zig cc -target x86_64-windows-gnu $(WARN) -O1 -fsanitize=undefined -fsanitize-trap=undefined \
	  -o build/win/test_undo_win.exe tests/test_undo_win.c src/undo/undo.c src/text/*.c
	@cd build/win && WINEDEBUG=-all WINEPREFIX=$${WINEPREFIX:-$$PWD/prefix} timeout 300 wine test_undo_win.exe 2>/dev/null \
	  || { echo "FAIL test_undo_win (Windows, under Wine)"; exit 1; }
.PHONY: undo-win
