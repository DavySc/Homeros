theorem add_zero' (n : Nat) : n + 0 = n := by
  induction n with
  | zero => rfl
  | succ k ih => simp

theorem and_swap (p q : Prop) (h : p ∧ q) : q ∧ p := by
  constructor
  · exact h.2
  · exact h.1

example (a b : Nat) (h : a ≤ b) : a < b + 1 := by
  exact?

theorem two_steps (x : Nat) (h : x = 2) : x + x = 4 := by
  subst h
  rfl
