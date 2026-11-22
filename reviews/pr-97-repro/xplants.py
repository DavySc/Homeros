import subprocess, sys
P='src/syntax/syntax.c'
plants=[
 ("update's result dropped (no redraw after an edit's colours change)", "	if (s->org) return orgfx_update(s->org, stop);", "	if (s->org) return orgfx_update(s->org, stop), 0;"),
 ("faces of a .org file never asked", "	if (s->org) {\n		orgfx_faces(s->org, a, z, face);\n		return;\n	}", "	if (s->org) return;"),
 ("edit passes nins for ndel", "orgfx_edit(s->org, t, pos, ndel, nins);", "orgfx_edit(s->org, t, pos, nins, ndel);"),
 ("edit passes pos + 1", "orgfx_edit(s->org, t, pos, ndel, nins);", "orgfx_edit(s->org, t, pos + 1, ndel, nins);"),
 ("faces asked one byte late", "orgfx_faces(s->org, a, z, face);", "orgfx_faces(s->org, a + 1, z, face + 1);"),
]
src=open(P).read()
for name,old,new in plants:
    assert src.count(old)==1, name
    open(P,'w').write(src.replace(old,new))
    r=subprocess.run('make -s test TESTS=syntax_org',shell=True,capture_output=True,text=True)
    out=(r.stdout+r.stderr).strip().splitlines()
    print(('caught    ' if r.returncode else 'NOT CAUGHT'), name, '|', out[-1][:150] if out else '')
open(P,'w').write(src)
r=subprocess.run('make -s test TESTS=syntax_org',shell=True,capture_output=True,text=True)
print('clean:', r.returncode, (r.stdout+r.stderr).strip().splitlines()[-1])
