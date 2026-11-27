# Package 22: TLS for the database clients (OpenSSL, from the distribution)
CPPFLAGS += $(shell pkg-config --cflags openssl)
LIBS += $(shell pkg-config --libs openssl)
# and OpenSSL for the Windows build (check.sh, wincc, the trial build),
# built by nix with zig cc (openssl.nix); copied out of the store so its time
# is the build's, as src/tools/build.mk's libghostty-vt
build/openssl-win/lib/libssl.a: src/sql/openssl.nix
	nix build -f $< -o build/openssl-win-nix
	rm -rf build/openssl-win && mkdir -p build/openssl-win
	cp -r build/openssl-win-nix/include build/openssl-win-nix/lib build/openssl-win/
	chmod -R u+w build/openssl-win
