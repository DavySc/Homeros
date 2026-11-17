#!/usr/bin/env python3
"""The stand-in Lean file of DESIGN.md's LSP measurement: 700 small
theorems in five shapes, `sorry` in theorems 15, 140, ..., 640 (6), the
rest proved.  python3 gen_lean.py > LongSorry.lean"""
for i in range(700):
    k = i % 5
    if k == 0:
        print(f"theorem t{i} (a b c : Nat) (h : a < b) : a + {i} < b + {i} + c := by\n  {'sorry' if i % 125 == 15 else 'omega'}\n")
    elif k == 1:
        print(f"theorem t{i} (l : List Nat) : (l ++ [{i}]).length = l.length + 1 := by\n  simp\n")
    elif k == 2:
        print(f"theorem t{i} (n : Nat) : 2 * n + {i} = n + n + {i} := by\n  induction n with\n  | zero => rfl\n  | succ m ih => omega\n")
    elif k == 3:
        print(f"def f{i} : Nat → Nat\n  | 0 => {i}\n  | n + 1 => f{i} n + 2\n\ntheorem t{i} (n : Nat) : f{i} n = {i} + 2 * n := by\n  induction n with\n  | zero => rfl\n  | succ m ih => simp [f{i}, ih]; omega\n")
    else:
        print(f"theorem t{i} (p q : Prop) (hp : p) (hq : q) : (p ∧ q) ∧ (q ∨ p) := by\n  exact ⟨⟨hp, hq⟩, Or.inl hq⟩" + ("\n" if i < 699 else ""))
