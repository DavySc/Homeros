# the picker's frecency decays exponentially (exp, log)
LIBS += -lm
# The terminal's VT engine, libghostty-vt 1.0.1, built by nix with Zig
# (ghostty.nix) and linked statically; copied out of the store so its time
# is the build's and make does not rebuild every time.
CPPFLAGS += -Ibuild/ghostty/include
OBJS += build/ghostty/libghostty-vt.a

build/ghostty/libghostty-vt.a: src/tools/ghostty.nix
	nix build -f $< -o build/ghostty-nix
	rm -rf build/ghostty && mkdir -p build/ghostty
	cp -r build/ghostty-nix/include build/ghostty-nix/lib/libghostty-vt.a build/ghostty/
	chmod -R u+w build/ghostty
