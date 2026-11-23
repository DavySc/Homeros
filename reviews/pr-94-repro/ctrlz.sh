set -u
export PATH=/home/claude/Homeros/tools-env/bin:$PATH
D=/tmp/claude-0/ctrlz; rm -rf $D/run; mkdir -p -m 700 $D/run; export XDG_RUNTIME_DIR=$D/run
cat > $D/sway.cfg <<'E'
output HEADLESS-1 resolution 1280x800 scale 1
default_border none
E
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 setsid sway -c $D/sway.cfg >$D/sway.log 2>&1 &
for i in $(seq 50); do ls $XDG_RUNTIME_DIR/wayland-* >/dev/null 2>&1 && break; sleep 0.1; done
export WAYLAND_DISPLAY=$(basename $(ls $XDG_RUNTIME_DIR/wayland-* | grep -v lock | head -1))
export SHELL=/bin/bash HOME=$D
cd $D && setsid /home/claude/wt/homeros $D/x.txt </dev/null >$D/h.log 2>&1 &
sleep 2
wtype -M ctrl -k period -m ctrl -s 800 "sleep 300" -k Return -s 800 -M ctrl z -m ctrl -s 800 "jobs; echo MARK" -k Return -s 1000
sleep 1
grim $D/shot.png
pkill -f "wt/homeros"; pkill sway
