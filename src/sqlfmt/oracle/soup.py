# token soup: random T-SQL words, symbols, literals and comments with random gaps and case
import random, sys
R = random.Random(int(sys.argv[1]))
W = ["select","from","where","join","left","inner","on","and","or","not","in","exists","group","by","order","having","insert","into","values","update","set","delete","merge","using","when","then","matched","case","else","end","begin","if","while","return","declare","exec","print","commit","rollback","tran","transaction","try","catch","throw","create","alter","procedure","proc","function","returns","as","table","view","with","union","all","top","distinct","is","null","like","between","output","cross","apply","outer","full","go","GO","nocount","int","varchar","getdate","count","x","t","a","b","dbo","sys","objects","Name","Date","raiserror","break","continue","goto","label","truncate","drop","index","open","close","fetch","next","cursor","for","bulk","save","waitfor","delay","default","over","partition","row_number","asc","desc","offset","rows","only","use","grant","revoke","deny"]
P = [",","(",")",";",".","=","<>","<",">",">=","<=","!=","+","-","*","/","%","::",":","*=","||"]
O = ["@x","@@rowcount","#tmp","[a b]","\"q\"","N'str'","'it''s'","'a'","1","2.5","0x1F","1e3"]
C = ["-- c\n","/* b */","/* a /* n */ */","--x\r\n"]
def case(w):
    r = R.random()
    return w.upper() if r < .4 else w if r < .8 else "".join(c.upper() if R.random()<.5 else c for c in w)
def ws():
    r = R.random()
    if r < .55: return " "
    if r < .7: return ""
    if r < .85: return "\n" + " " * R.randrange(0, 9)
    if r < .9: return "\n\n\n"
    if r < .95: return "\t"
    return "\r\n"
out = []
for _ in range(R.randrange(5, 120)):
    r = R.random()
    if r < .55: out.append(case(R.choice(W)))
    elif r < .75: out.append(R.choice(P))
    elif r < .93: out.append(R.choice(O))
    else: out.append(R.choice(C))
    out.append(ws())
if R.random() < .3: out.insert(0, "﻿")
sys.stdout.write("".join(out))
