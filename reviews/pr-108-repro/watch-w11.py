import tempfile, os, pty, sys, time, select, subprocess
exe = sys.argv[1]; path = os.path.join(tempfile.mkdtemp(), 'w.txt')
open(path, 'w').write('hello\n')
env = dict(os.environ, TERM='xterm-256color', XDG_STATE_HOME=tempfile.mkdtemp())
pid, fd = pty.fork()
if pid == 0:
    os.execve(exe, [exe, '-t', path], env)
def read(t):
    out = b''; end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: out += os.read(fd, 65536)
            except OSError: break
    return out
read(1.5)
ino = [f for f in os.listdir('/proc/%d/fd' % pid) if 'inotify' in os.readlink('/proc/%d/fd/%s' % (pid, f))]
st = subprocess.Popen(['strace', '-f', '-e', 'trace=poll,ppoll,epoll_wait,epoll_ctl', '-p', str(pid)], stderr=subprocess.PIPE)
time.sleep(0.5)
os.write(fd, b'j'); read(0.5)
with open(path, 'a') as g: g.write('outside\n')
read(0.8)
os.write(fd, b'x'); out = read(1.0)
st.terminate(); log = st.stderr.read().decode()
polls = [l for l in log.split('\n') if 'poll(' in l]
print(os.path.basename(exe), 'inotify fd', ino, '| last poll:', polls[-1][:160] if polls else None)
print('  edit after outside change shows:', [m for m in (b'WARNING', b'changed', b'written', b'W11') if m in out])
os.kill(pid, 9)
