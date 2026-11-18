# Review of PR 51: peak RSS of a command (ru_maxrss), 5 runs.  usage: rss.py CMD ARG...
import os, resource, subprocess, sys
r = []
for i in range(5):
    pid = os.fork()
    if pid == 0:
        subprocess.run(sys.argv[1:], stdout=subprocess.DEVNULL)
        print(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss); sys.stdout.flush(); os._exit(0)
    os.waitpid(pid, 0)
