#!/bin/sh
# PR 138's review, on Linux: a browser that xdg-open starts itself (none was
# running) and that writes a line to stderr now and then, as browsers do.
# Does it outlive the editor?  Run from the repository's top:
#   sh reviews/pr-138-repro/browser.sh pr     the PR: the editor's proc_open
#   sh reviews/pr-138-repro/browser.sh main SERVE   main: SERVE (main's
#       homeros-infoview) runs the opener, told to by "!"
set -eu
d=$(mktemp -d)
cat > "$d/xdg-open" <<X
#!/bin/sh
exec sh -c 'echo \$\$ > $d/pid; while sleep 0.2; do echo log >&2; done'
X
chmod +x "$d/xdg-open"
mkdir -p "$d/root" build
if [ "$1" = pr ]; then
	cc -Iinclude -o build/browser reviews/pr-138-repro/browser.c src/loop/loop.c src/loop/timer.c
	PATH=$d:$PATH build/browser /tmp/x.html
else
	python3 - "$2" "$d" <<'PY'
import subprocess, sys, time
p = subprocess.Popen([sys.argv[1], sys.argv[2] + '/root', sys.argv[2] + '/xdg-open'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
p.stdout.readline()
p.stdin.write(b'!\n'); p.stdin.flush()
time.sleep(0.5)
PY
fi
sleep 1.5
# a zombie nobody reaps still has a pid: ask for its state
if grep -q '^State:.[RSD]' "/proc/$(cat "$d/pid")/status" 2>/dev/null; then echo "$1: the browser lives on"; kill "$(cat "$d/pid")"
else echo "$1: the browser DIED with the editor"; fi
rm -rf "$d"
