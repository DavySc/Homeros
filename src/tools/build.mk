# the picker's frecency decays exponentially (exp, log)
LIBS += -lm
# The terminal's VT engine, libghostty-vt 1.0.1, built by nix with Zig
# (ghostty.nix) and linked statically; copied out of the store so its time
# is the build's and make does not rebuild every time.
CPPFLAGS += -Ibuild/ghostty/include -DGHOSTTY_STATIC
OBJS += build/ghostty/libghostty-vt.a

build/ghostty/libghostty-vt.a: src/tools/ghostty.nix
	nix build -f $< -o build/ghostty-nix
	rm -rf build/ghostty && mkdir -p build/ghostty
	cp -r build/ghostty-nix/include build/ghostty-nix/lib/libghostty-vt.a build/ghostty/
	chmod -R u+w build/ghostty
# the Windows build's (src/tools/check.sh, the trial build); the headers are
# the same
build/ghostty/windows.lib: src/tools/ghostty.nix build/ghostty/libghostty-vt.a
	nix build -f $< --argstr target x86_64-windows-gnu -o build/ghostty-nix-win
	cp build/ghostty-nix-win/lib/ghostty-vt-static.lib $@
	chmod u+w $@
