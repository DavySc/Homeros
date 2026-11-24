/-- A point: doc comment with `code`. -/
structure Point where
  x : Nat
  y : Nat
deriving Repr

namespace Point
def add (p q : Point) : Point := { x := p.x + q.x, y := p.y + q.y }
-- a line comment
theorem add_comm' (p q : Point) : (add p q).x = (add q p).x := by
  simp [add]
  omega
end Point

def greet (name : String) : String := s!"hello {name}" ++ "!"

theorem foo (a b : Nat) (h : a < b) : a + 1 < b + 1 := by
  have h2 : a + 1 ≤ b := h
  intro
  sorry

example (α : Type) (xs : List α) : xs.length = xs.length := rfl
#eval greet "λ𝓝"
instance : Add Point := ⟨Point.add⟩
open Nat in
theorem bar (n : Nat) : n + 0 = n := by
  induction n with
  | zero => rfl
  | succ m ih => simp
