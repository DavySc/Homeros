#!/bin/bash
# %F in Neovim 0.12 against Homeros's file_full, case by case.
# usage: pctF.sh <nvim> <full_probe>     (prints one block per case)
N=$1; P=$2
T=$(mktemp -d); T=$(realpath "$T"); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/home/u/proj/sub" "$T/home/u2/proj" "$T/other/real" "$T/home/u/sp ace"
echo x > "$T/home/u/proj/a.txt"; echo x > "$T/home/u/proj/sub/b.txt"; echo x > "$T/home/u2/proj/c.txt"; echo x > "$T/other/real/r.txt"
ln -s "$T/other/real/r.txt" "$T/home/u/proj/lnf"      # symlinked file
ln -s "$T/other/real" "$T/home/u/proj/lnd"            # symlinked directory
ln -s "$T/home/u" "$T/homelink"                       # $HOME itself a symlink
printf 'x\n' > "$T/home/u/proj/c"$'\001'"x"
# nv <HOME> <cwd> <file argument or ''> [ex commands...]: prints b_ffname and %F after the commands
nv() {
	local home=$1 cwd=$2 arg=$3; shift 3
	local cmds=()
	for c in "$@"; do cmds+=(-c "$c"); done
	(cd "$cwd" && HOME=$home XDG_CONFIG_HOME=$T/xdg XDG_DATA_HOME=$T/xdg XDG_STATE_HOME=$T/xdg "$N" --clean --headless -n ${arg:+"$arg"} "${cmds[@]}" \
	  -c 'call writefile(["nvim  ffname=" .. bufname("%")->empty() ?? "" .. "", "nvim  name=[" .. nvim_buf_get_name(0) .. "] %F=[" .. nvim_eval_statusline("%F", {}).str .. "] %f=[" .. nvim_eval_statusline("%f", {}).str .. "]"][1:], "/dev/stdout")' -c 'qa!' 2>&1 | sed "s|$T|T|g;s|${T#/}|T-rel|g;s|//[0-9]*:|//PID:|g")
}
hm() { (cd "$1" && shift && "$P" "$@" 2>&1 | sed "s|$T|T|g;s|^|homeros file_full=[|;s|\$|]|"); }
both() { # <label> <HOME> <cwd> <arg>
	echo "== $1   (HOME=${2/$T/T} cwd=${3/$T/T} arg=${4/$T/T})"; nv "$2" "$3" "$4"; hm "$3" "$4"; }
H=$T/home/u
both "relative"                 $H $H/proj a.txt
both "relative with dir"        $H $H proj/sub/b.txt
both "dot-slash"                $H $H/proj ./a.txt
both "dot-dot"                  $H $H/proj/sub ../a.txt
both "dot-dot in the middle"    $H $H proj/sub/../a.txt
both "double slash"             $H $H proj//a.txt
both "absolute"                 $H / $H/proj/a.txt
both "symlinked file"           $H $H/proj lnf
both "symlinked dir"            $H $H/proj lnd/r.txt
both "symlinked dir, absolute"  $H / $H/proj/lnd/r.txt
both "dot-dot after a symlink"  $H $H/proj lnd/../a.txt
both "sibling of HOME (u2)"     $H / $T/home/u2/proj/c.txt
both "HOME itself"              $H $T/home u
both "HOME=/"                   / $H/proj a.txt
both "HOME with trailing slash" $H/ $H/proj a.txt
both "HOME a symlink, file via the link" $T/homelink $T/homelink/proj a.txt
both "HOME a symlink, file via the real path" $T/homelink $H/proj a.txt
both "HOME real, file via a link to HOME" $H / $T/homelink/proj/a.txt
both "HOME unset-like (empty)"  "" $H/proj a.txt
both "new file"                 $H $H/proj new.txt
both "new file, dir missing"    $H $H/proj nodir/new.txt
both "space in a directory"     $H $H "sp ace/n.txt"
both "control char in the name" $H $H/proj "c"$'\001'"x"
both "tilde argument"           $H / "~/proj/a.txt"
echo "== no name"; nv $H $H/proj ""
echo "== :cd after open"; nv $H $H/proj a.txt "cd sub"
echo "== :cd to / after open"; nv $H $H/proj a.txt "cd /"
echo "== :w new name from a named buffer (the buffer keeps its name)"; nv $H $H/proj a.txt "w! w1.txt"
echo "== :w name from a no-name buffer (the buffer takes it)"; nv $H $H/proj "" "w! w2.txt"
echo "== :saveas"; nv $H $H/proj a.txt "saveas! sub/s.txt"
echo "== :file newname"; nv $H $H/proj a.txt "file sub/f.txt"
echo "== :e other"; nv $H $H/proj a.txt "e sub/b.txt"
echo "== :w after cd, new file typed relative"; nv $H $H/proj "" "cd sub" "w! w3.txt" "cd /"
echo "== the directory is renamed after open"; nv $H $H/proj sub/b.txt "call rename('sub', 'sub2')"
echo "== new file in a missing dir, the dir made later, :w"; nv $H $H/proj nodir2/n.txt "call mkdir('nodir2')" "w"
echo "== scratch/nofile buffer with a name"; nv $H $H/proj a.txt "setlocal buftype=nofile"
echo "== help buffer"; nv $H $H/proj "" "help help"
echo "== terminal buffer"; nv $H $H/proj "" "terminal true"
echo "== quickfix"; nv $H $H/proj "" "copen"
