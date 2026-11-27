# zig cc's arguments for the SQL clients on Windows: OpenSSL (openssl.nix,
# build.mk) and Winsock, the certificate store and SSPI, for the Windows
# tests and the trial build that link src/sql; built first if missing
[ -f build/openssl-win/lib/libssl.a ] || make -s build/openssl-win/lib/libssl.a
SQL_WIN="-Ibuild/openssl-win/include build/openssl-win/lib/libssl.a build/openssl-win/lib/libcrypto.a -lws2_32 -lcrypt32 -lsecur32"
