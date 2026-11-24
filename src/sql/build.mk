# Package 22: TLS for the database clients (OpenSSL, from the distribution)
CPPFLAGS += $(shell pkg-config --cflags openssl)
LIBS += $(shell pkg-config --libs openssl)
