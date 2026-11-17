import sys,time,subprocess; sys.path.insert(0,"/opt/ne/ref"); from drive import *
def k(*a,lit=False): keys(*a,lit=lit); time.sleep(0.3)
def cpu(name):
    pid=subprocess.run(["pgrep","-n",name],capture_output=True,text=True).stdout.strip()
    return int(open(f"/proc/{pid}/stat").read().split()[13])
for ed,cmd,pn in (("nvim","nvim -u NONE -i NONE -n /tmp/ne-t2/m.txt","nvim"),("helix","hx /tmp/ne-t2/m.txt","hx")):
    open("/tmp/ne-t2/m.txt","w").write("x\n")
    start(cmd,{"HOME":f"/opt/ne/ref/home-{ed}","TERM":"xterm-256color"}); time.sleep(1.5)
    if ed=="nvim": k("qq",lit=True); k("Ay",lit=True); k("Escape"); k("q",lit=True); k("100000000@q",lit=True)
    else: k("Q",lit=True); k("Ay",lit=True); k("Escape"); k("Q",lit=True); k("100000000q",lit=True)
    c0=cpu(pn); time.sleep(3); c1=cpu(pn); keys("C-c"); time.sleep(1.0); c2=cpu(pn); time.sleep(1.0); c3=cpu(pn)
    k("Escape"); k(":w",lit=True); k("Enter"); time.sleep(1)
    n=len(open("/tmp/ne-t2/m.txt").read())
    print(f"{ed}: cpu ticks in 3 s of macro={c1-c0}; in 2nd second after Ctrl-C={c3-c2}; :w after Ctrl-C wrote file of {n} bytes")
    tm("kill-server")
