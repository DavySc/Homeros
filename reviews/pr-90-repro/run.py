#!/usr/bin/env python3
"""run.py PROBE FILE KEYS [h]: the probe in a fresh state directory; prints its
first lines, its exit status and its peak memory."""
import os, resource, subprocess, sys, tempfile
r = subprocess.run(sys.argv[1:], capture_output=True, env=dict(os.environ, XDG_STATE_HOME=tempfile.mkdtemp()), timeout=600)
for l in r.stdout.decode(errors="replace").split("\n"):
    if l and not l.startswith("|") and not l.startswith(" hl"): print("  ", l[:140])
print("   rc=%d peak %d MB" % (r.returncode, resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss // 1024))
