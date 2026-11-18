#!/usr/bin/env bash
# Review of PR 53: 'fileformat' and the crash journal.  The driver's "crash"
# mode exits without closing, as a kill does, so the journal stays.
# usage: crash.sh DRIVER [MAIN_DRIVER]   (MAIN_DRIVER: eol_drv.c built on main, for a journal from before the PR)
drv=$1; old=${2:-}
d=$(mktemp -d); export XDG_STATE_HOME=$d/state
keys() { printf '%s ' "$@"; }
show() { od -An -c "$1" | tr -s ' ' | tr -d '\n'; echo; }
rc=0
want() { got=$(show $d/f); [ "$got" = "$2" ] && echo "ok    $1: $got" || { echo "FAIL  $1: got $got, want $2"; rc=1; }; }

printf 'a\r\nb\r\n' > $d/f
keys 105 120 27 | $drv $d/f crash >/dev/null               # ix<Esc>, then killed
$drv $d/f </dev/null | sed 's/^/      /' | sed "s|$d||g"
want "dos buffer killed after ix<Esc>, recovered, :w" " x a \r \n b \r \n"

printf 'a\r\nb' > $d/f
keys 71 111 122 27 | $drv $d/f crash >/dev/null            # Goz<Esc>
$drv $d/f </dev/null >/dev/null
want "dos noeol buffer killed after Goz<Esc>, recovered, :w" " a \r \n b \r \n z"

printf 'a\r\nb\r\n' > $d/f
keys 100 100 100 100 | $drv $d/f crash >/dev/null          # dddd: emptied
$drv $d/f </dev/null >/dev/null
want "dos buffer emptied, killed, recovered, :w" ""

printf 'a\r\nb\r\n' > $d/f
keys 100 100 100 100 105 113 27 | $drv $d/f crash >/dev/null   # emptied, then iq<Esc>
$drv $d/f </dev/null >/dev/null
want "dos buffer emptied, iq<Esc>, killed, recovered, :w" " q \r \n"

printf 'a\r\nb\r\n' > $d/f
keys 105 120 27 117 | $drv $d/f crash >/dev/null           # ix<Esc>u: back to the file's text
$drv $d/f </dev/null | grep -c recovered | sed 's/^/      "recovered" messages after an undone edit (0: the journal is dropped): /'
want "dos buffer, edit undone, killed, reopened, :w" " a \r \n b \r \n"

# killed twice: the journal continues after a recovery
printf 'a\r\nb\r\n' > $d/f
keys 105 120 27 | $drv $d/f crash >/dev/null
keys 105 121 27 | $drv $d/f crash >/dev/null
$drv $d/f </dev/null >/dev/null
want "dos buffer killed, recovered, edited, killed again, recovered, :w" " y x a \r \n b \r \n"

# the file is rewritten as LF by another program while the journal waits: recovered as dos (the journal's format), plain :w refused
printf 'a\r\nb\r\n' > $d/f
keys 105 120 27 | $drv $d/f crash >/dev/null
sleep 0.05; printf 'a\nb\n' > $d/f
$drv $d/f </dev/null | sed -n 's/^write //p' | cut -c1-60 | sed 's/^/      /'
want "dos journal, file made LF outside, plain :w" " a \n b \n"
rm -rf $d/state; printf 'a\r\nb\r\n' > $d/f
keys 105 120 27 | $drv $d/f crash >/dev/null
sleep 0.05; printf 'a\nb\n' > $d/f
$drv $d/f force </dev/null >/dev/null
want "dos journal, file made LF outside, :w! (the journal's format wins)" " x a \r \n b \r \n"

if [ -n "$old" ]; then
	printf 'a\r\nb\r\n' > $d/f
	keys 105 120 27 | $old $d/f crash >/dev/null
	$drv $d/f </dev/null | sed -n '1,2p' | sed 's/^/      /' | sed "s|$d||g"
	want "journal written before the PR (CRs in the text, no J_DOS), recovered by the PR, :w" " x a \r \n b \r \n"
	# and with no edit left to recover: the old journal's base equals the file's bytes
	printf 'a\r\nb\r\n' > $d/f
	keys 105 120 27 117 | $old $d/f crash >/dev/null
	$drv $d/f </dev/null | sed -n '1p' | sed 's/^/      /' | sed "s|$d||g"
	want "old journal, edit undone, opened by the PR, :w" " a \r \n b \r \n"
fi
rm -rf $d
exit $rc
