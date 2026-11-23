# zig cc's arguments for libghostty-vt on Windows (build.mk's
# build/ghostty/windows.lib), for the Windows tests that link the terminal:
# src/tools/check.sh and src/git/check.sh source it
GHOSTTY_WIN="-Ibuild/ghostty/include -DGHOSTTY_STATIC build/ghostty/windows.lib"
