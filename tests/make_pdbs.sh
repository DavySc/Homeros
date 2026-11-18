#!/bin/bash
# make_pdbs.sh <out dir>: PDBs made here with clang and lld-link from the subjects that need no
# Windows headers, plain and optimised, one with 8 KB pages, and one with 400 object files
# so that the directory spans two pages. MSVC's PDBs come from the Windows runner (rdi/*.pdb).
set -eu
out=$1; mkdir -p "$out/obj"; here=$(cd "$(dirname "$0")/.." && pwd)
link() { lld-link /nologo /debug /nodefaultlib /entry:main /subsystem:console /dynamicbase:no "$@"; }
for s in steps crash deep; do
  for o in O0 O2; do
    clang --target=x86_64-pc-windows-msvc -g -gcodeview -$o -ffreestanding -c "$here/subjects/$s.c" -o "$out/obj/$s-$o.obj"
    link "$out/obj/$s-$o.obj" /out:"$out/$s-$o.exe" /pdb:"$out/$s-$o.pdb"
  done
done
link "$out/obj/steps-O0.obj" /pdbpagesize:8192 /out:"$out/steps-8k.exe" /pdb:"$out/steps-8k.pdb"
mkdir -p "$out/many"; cd "$out/many"
for i in $(seq 400); do echo "int f$i(int x) { int y = x + $i; return y * 2; }" > "f$i.c"; done
echo "int f1(int); int main(void) { return f1(0); }" > main.c
clang --target=x86_64-pc-windows-msvc -g -gcodeview -O0 -ffreestanding -c ./*.c
link ./*.o /out:"$out/many.exe" /pdb:"$out/many.pdb"
cd "$out"; rm -rf obj many
