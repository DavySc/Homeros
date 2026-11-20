#!/usr/bin/env python3
"""PR 27 review: a stand-in for the PR's "3,200-line generated file" (its
generator is not in the repo).  800 theorems of 4 lines, every one named
tNNN and proved by `by`; every fourth uses sorry, so Lean warns on its name.
usage: gen_lean.py > Long.lean"""
for i in range(800):
    print(f"theorem t{i} (a b c d : Nat) (h : a = b) (g : c = d) : a + c + {i} = b + d + {i} := by")
    print("  subst h")
    print("  sorry" if i % 4 == 0 else "  omega")
    print()
