import subprocess,statistics
words="select from where join inner left outer group order having insert into values update set delete declare int varchar nvarchar decimal date datetime bit begin end else while return exec create table procedure and not null exists case when then top distinct union count sum max min getdate isnull coalesce cast convert try catch throw transaction commit rollback nocount with nolock output merge using over partition row_number bigint datetime2 money uniqueidentifier identity primary key index view function trigger alter drop truncate print raiserror dateadd datediff len substring replace charindex upper lower ltrim rtrim scope_identity object_id".split()
def pos(probe,w,n):
    r=subprocess.run([probe,'w.sql','p.sql',w[:n]],capture_output=True,text=True,env={'MAXI':'99','PATH':'/usr/bin'}).stdout.split('\n')[0]
    items=[x.split('[')[0] for x in r.split(':',1)[1].split()]
    return items.index(w)+1 if w in items else 99
for n in (2,3):
    better=worse=same=0; rows=[]
    for w in words:
        if len(w)<=n: continue
        a=pos('/home/claude/mainwt/build/o2/probe',w,n); b=pos('/home/claude/Homeros/build/o2/probe',w,n)
        if b<a: better+=1
        elif b>a: worse+=1; rows.append((w,a,b))
        else: same+=1
    print(n,'letters: better',better,'worse',worse,'same',same); print('  worse:',rows)
