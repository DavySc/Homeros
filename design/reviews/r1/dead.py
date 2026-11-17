# Editor writes a large didOpen with a blocking write while the server is itself blocked writing to the editor.
import subprocess, sys, threading, os
child = "import sys; sys.stdout.write('d'*200000); sys.stdout.flush(); sys.stdin.read()"   # server publishes 200 KB before reading
p = subprocess.Popen([sys.executable, "-c", child], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
done = threading.Event()
def editor():
    p.stdin.write(b"x" * 1_000_000); p.stdin.flush()   # blocking write of a 1 MB didOpen, single thread, no poll
    done.set()
threading.Thread(target=editor, daemon=True).start()
print("blocking write finished within 5 s:", done.wait(5), "(False = deadlock: both sides blocked in write)")
p.kill()
