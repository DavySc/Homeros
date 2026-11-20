def 𝔸 : Nat := "x"
example : ∀ n : Nat, n = n := by intro n; exact foo
#eval 𝔸 + 𝔹
