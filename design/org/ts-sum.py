# Summarise org files with tree-sitter-org (nvim-orgmode's grammar), same counts as el-sum.el.
import ctypes, json, sys, warnings, tree_sitter as ts
warnings.simplefilter("ignore")
lib = ctypes.cdll.LoadLibrary(sys.argv[1]); lib.tree_sitter_org.restype = ctypes.c_void_p
P = ts.Parser(ts.Language(lib.tree_sitter_org()))
KW = {b"TODO", b"NEXT", b"WAIT", b"DONE", b"CANCELLED"}
def walk(n):
    yield n
    for c in n.children: yield from walk(c)
res = {}
for f in sys.argv[2:]:
    src = open(f, "rb").read(); nodes = list(walk(P.parse(src).root_node))
    by = lambda t: [n for n in nodes if n.type == t]
    hs = by("headline")
    lv = [len(h.child_by_field_name("stars").text) for h in hs]
    def todo(h):
        it = h.child_by_field_name("item")
        return it is not None and it.named_child_count > 0 and it.named_children[0].text in KW
    names = [(b.child_by_field_name("name").text.lower() if b.child_by_field_name("name") else b"") for b in by("block")]
    res[f.split("/")[-1]] = dict(headlines=len(hs), h1=lv.count(1), h3plus=sum(l >= 3 for l in lv),
        todo=sum(map(todo, hs)), tagged=sum(1 for h in hs if h.child_by_field_name("tags") is not None),
        src_blocks=names.count(b"src"), example_blocks=names.count(b"example"), tables=len(by("table")),
        timestamps=len(by("timestamp")), property_drawers=len(by("property_drawer")), plain_lists=len(by("list")),
        errors=sum(1 for n in nodes if n.type == "ERROR" or n.is_missing))
print(json.dumps(res))
