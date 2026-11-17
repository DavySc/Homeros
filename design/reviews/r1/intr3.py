import sys,time,subprocess; sys.path.insert(0,"/opt/ne/ref"); from drive import *
def k(*a,lit=False): keys(*a,lit=lit); time.sleep(0.3)
def cpu():
    return sum(int(open(f"/proc/{p}/stat").read().split()[13]) for p in subprocess.run(["pgrep","nvim"],capture_output=True,text=True).stdout.split())
open("/tmp/ne-t2/m.txt","w").write("x\n")
start("nvim -u NONE -i NONE -n /tmp/ne-t2/m.txt",{"HOME":"/opt/ne/ref/home-nvim","TERM":"xterm-256color"}); time.sleep(1.5)
k(":let i=0 | while 1 | let i+=1 | endwhile",lit=True); keys("Enter")
c0=cpu(); time.sleep(3); c1=cpu()
keys("C-c"); time.sleep(1.0); c2=cpu(); time.sleep(1.0); c3=cpu()
k("Escape"); k("iZ",lit=True); k("Escape"); k(":w",lit=True); k("Enter"); time.sleep(1)
print(f"nvim endless :while: cpu ticks in 3 s={c1-c0}; in 2nd second after Ctrl-C={c3-c2}; then typed Z and saved: {open('/tmp/ne-t2/m.txt').read()!r}")
tm("kill-server")
