import resource, subprocess, sys, time
t=time.time(); r=subprocess.run(sys.argv[1:], capture_output=True, text=True)
u=resource.getrusage(resource.RUSAGE_CHILDREN)
print(r.stdout.strip(), f"wall={time.time()-t:.2f}s maxrss={u.ru_maxrss//1024}MB", r.stderr.strip()[-200:])
