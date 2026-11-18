#!/bin/bash
# make_pdbs.sh <out dir>: PDBs made here with clang and lld-link from the subjects that need no
# Windows headers, plain and optimised, one with 8 KB pages, and one with 400 object files
# so that the directory spans two pages. MSVC's PDBs come from the Windows runner (rdi/*.pdb).
set -eu
out=$1; mkdir -p "$out/obj"; here=$(cd "$(dirname "$0")/.." && pwd)
link() { lld-link /nologo /debug /nodefaultlib /entry:main /subsystem:console /dynamicbase:no "$@"; }
for s in steps crash deep blocks gap; do
  for o in O0 O2; do
    clang --target=x86_64-pc-windows-msvc -g -gcodeview -$o -ffreestanding -c "$here/subjects/$s.c" -o "$out/obj/$s-$o.obj"
    link "$out/obj/$s-$o.obj" /out:"$out/$s-$o.exe" /pdb:"$out/$s-$o.pdb"
  done
done
link "$out/obj/steps-O0.obj" /pdbpagesize:8192 /out:"$out/steps-8k.exe" /pdb:"$out/steps-8k.pdb"
# column records, and one function whose lines come from two files
clang --target=x86_64-pc-windows-msvc -g -gcodeview -gcolumn-info -O0 -ffreestanding -c "$here/subjects/steps.c" -o "$out/obj/cols.obj"
link "$out/obj/cols.obj" /out:"$out/cols.exe" /pdb:"$out/cols.pdb"
printf 'int main(void)\n{\n  volatile int a = 1;\n#line 100 "other.c"\n  a += 2;\n  a += 3;\n#line 7 "twofiles.c"\n  return a;\n}\n' > "$out/obj/twofiles.c"
clang --target=x86_64-pc-windows-msvc -g -gcodeview -O0 -ffreestanding -c "$out/obj/twofiles.c" -o "$out/obj/twofiles.obj"
link "$out/obj/twofiles.obj" /out:"$out/twofiles.exe" /pdb:"$out/twofiles.pdb"
# the second function's lines come first but it is placed second, right at the end of main:
# main's end marker and its first line share an offset, and the marker is the later record
printf '__attribute__((section(".text$b"), aligned(1))) int second(int x)\n{\n  return x + 1;\n}\n__attribute__((section(".text$a"), aligned(1))) int main(void)\n{\n  return second(1);\n}\n' > "$out/obj/reversed.c"
clang --target=x86_64-pc-windows-msvc -g -gcodeview -O0 -ffreestanding -falign-functions=1 -c "$out/obj/reversed.c" -o "$out/obj/reversed.obj"
link "$out/obj/reversed.obj" /out:"$out/reversed.exe" /pdb:"$out/reversed.pdb"
# Damaged copies of a good file. deleted.pdb: stream 0, which has no bytes anyway, marked deleted
# (size 0xffffffff); the tools still read it. In bad/, as fuzz seeds only: longlast.pdb, whose
# last stream claims one page more than the directory lists, and nocontribs.pdb, whose section contributions have an
# unknown version, so it has public symbols and no unit map, noids.pdb, whose id stream is empty, and farid.pdb, whose first inlined call names
# an id far past the last.
mkdir -p "$out/bad"
python3 -I -c 'import struct, sys
good = open(sys.argv[1], "rb").read()
u32 = lambda d, at: struct.unpack_from("<I", d, at)[0]
page, pages = u32(good, 32), u32(good, 52)
at = lambda k: u32(good, pages * page + 4 * (k // page)) * page + k % page   # where byte k of the directory is in the file
def patched(where, value):
    d = bytearray(good); struct.pack_into("<I", d, where, value); return d
open(sys.argv[2] + "/deleted.pdb", "wb").write(patched(at(4), 0xffffffff))
streams = u32(good, at(0))
open(sys.argv[2] + "/bad/noids.pdb", "wb").write(patched(at(4 + 4 * 4), 0))        # stream 4, the ids, without bytes
open(sys.argv[2] + "/bad/longlast.pdb", "wb").write(patched(at(4 * streams), u32(good, at(4 * streams)) + page))
sizes = [u32(good, at(4 + 4 * i)) for i in range(streams)]
first = 4 + 4 * streams + 4 * sum(-(-n // page) for n in sizes[:3])            # the page list of stream 3, the DBI stream
dbi = lambda k: u32(good, at(first + 4 * (k // page))) * page + k % page
open(sys.argv[2] + "/bad/nocontribs.pdb", "wb").write(patched(dbi(64 + u32(good, dbi(24))), 0))' "$out/steps-O0.pdb" "$out"
python3 -I -c 'import re, sys
d = bytearray(open(sys.argv[1], "rb").read())
at = re.search(rb"\x4d\x11.{8}.\x10\x00\x00", d, re.S).start()               # S_INLINESITE: parent, end, then the id
d[at + 10:at + 14] = b"\xff\xff\xff\x7f"
open(sys.argv[2], "wb").write(d)' "$out/gap-O2.pdb" "$out/bad/farid.pdb"
mkdir -p "$out/many"; cd "$out/many"
for i in $(seq 400); do echo "int f$i(int x) { int y = x + $i; return y * 2; }" > "f$i.c"; done
echo "int f1(int); int main(void) { return f1(0); }" > main.c
clang --target=x86_64-pc-windows-msvc -g -gcodeview -O0 -ffreestanding -c ./*.c
link ./*.o /out:"$out/many.exe" /pdb:"$out/many.pdb"
cd "$out"; rm -rf obj many
