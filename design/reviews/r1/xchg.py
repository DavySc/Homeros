import ctypes, os, time
libc = ctypes.CDLL(None, use_errno=True); AT=-100; XCHG=2
d="/tmp/ne-t2/x"; os.makedirs(d,exist_ok=True); f=d+"/file"; tmp=d+"/.file.tmp"
open(f,"w").write("v0 loaded by editor\n"); seen=os.stat(f).st_ino
assert os.stat(f).st_ino==seen                      # editor: check passes
open(d+"/o","w").write("OUTSIDE change in the window\n"); os.rename(d+"/o",f)   # another program replaces the file
open(tmp,"w").write("editor buffer\n")
r=libc.renameat2(AT,tmp.encode(),AT,f.encode(),XCHG); assert r==0, os.strerror(ctypes.get_errno())
print("file now:", open(f).read().strip()); print("swapped out:", open(tmp).read().strip())
print("swapped-out inode is the one we loaded:", os.stat(tmp).st_ino==seen, "=> outside change detected, nothing lost")
# identity check: same-size in-place rewrites
g=d+"/g"; open(g,"w").write("aaaa"); same=0; N=2000
for i in range(N):
    a=os.stat(g); fd=os.open(g,os.O_WRONLY); os.write(fd,b"bbbb" if i%2 else b"cccc"); os.close(fd); b=os.stat(g)
    same += (a.st_ino,a.st_size,a.st_mtime_ns)==(b.st_ino,b.st_size,b.st_mtime_ns)
print(f"in-place same-size rewrite left (inode,size,mtime_ns) unchanged in {same} of {N} tries")
big=d+"/big"; buf=b"x"*(100<<20)
for i in range(3):
    t=time.time(); fd=os.open(big,os.O_WRONLY|os.O_CREAT|os.O_TRUNC); os.write(fd,buf); t1=time.time(); os.fsync(fd); os.close(fd); print(f"100 MB write {t1-t:.2f}s fsync {time.time()-t1:.2f}s")
