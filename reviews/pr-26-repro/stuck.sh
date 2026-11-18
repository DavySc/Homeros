#!/bin/sh
# Review of PR 26, finding 1: 64 keys of one unfinished Normal command fill
# pend[] (src/keys/normal.c); every later key, Esc included, is dropped and
# the command never ends.  The driver ends a pending command with Esc, so it
# never returns.  usage: stuck.sh DRIVER   (prints "stuck" when it hangs)
printf 'ab\n' > /tmp/stuck.txt
python3 -c "print(' '.join(['49'] * 63 + ['120']))" | timeout 10 "$1" /tmp/stuck.txt; echo "  63 digits then x: exit $?"
python3 -c "print(' '.join(['49'] * 64 + ['120']))" | timeout 10 "$1" /tmp/stuck.txt; r=$?; echo "  64 digits then x: exit $r"
[ $r = 124 ] && echo stuck
