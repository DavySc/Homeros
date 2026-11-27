import sys,subprocess
def diff(s):
    open('m.lean','wb').write(s)
    o=subprocess.run(['./cmp.sh','m.lean'],capture_output=True,text=True).stdout
    return ' 0 of ' not in o, o
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read().decode()
    # greedy char-level minimise
    assert diff(d.encode())[0]
    n=len(d)//2
    while n>=1:
        i=0
        while i<len(d):
            c=d[:i]+d[i+n:]
            if diff(c.encode())[0]: d=c
            else: i+=n
        n//=2
    print(repr(d)); print(diff(d.encode())[1])
