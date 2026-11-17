/-!
Undo law for the editor's edit log, for every text and every edit sequence
(no size bound, unlike the TLC model specs/Undo.tla, which checks texts of
up to 6 elements and 4 operations).

An edit inserts or deletes one element at a position; a delete records
the element it removes, so it can be inverted.  `Edit.apply` fails (`none`)
on a position that does not exist, or on a delete whose recorded element is
not there.  Theorem `undo_group`: if a group of edits applied to `s` gives
`t`, then the inverted edits, in reverse order, applied to `t` give back
exactly `s`.  This is the inversion law for one group.  The stack rules
(close the open group before undo, clear redo on a new edit) are checked
only by TLC in specs/Undo.tla, up to 4 operations.  (A `redo_group` theorem
was removed after review: it restated its own hypothesis.)

Undo tree (added 2026-11-17).  Each tree node stores the group that takes
its parent's text to its own.  Theorem `jump`: if the groups on the path
from a common ancestor `a` down to the current node give the current text,
and the groups from `a` down to a target node give the target text, then
undoing the first path (inverses, in reverse) and redoing the second lands
on exactly the target text.  u, C-r, g-, g+ and :undo N are all such
jumps.  That the editor picks the right ancestor and remembers the
last-visited child is checked by TLC in specs/UndoTree.tla.
-/

inductive Edit (α : Type) where
  | ins (p : Nat) (c : α)
  | del (p : Nat) (c : α)

set_option linter.unusedSectionVars false
variable {α : Type} [DecidableEq α]

def Edit.inv : Edit α → Edit α
  | .ins p c => .del p c
  | .del p c => .ins p c

theorem Edit.inv_inv (e : Edit α) : e.inv.inv = e := by cases e <;> rfl

def Edit.apply : Edit α → List α → Option (List α)
  | .ins p c, s => if p ≤ s.length then some (s.take p ++ c :: s.drop p) else none
  | .del p c, s => if s[p]? = some c then some (s.take p ++ s.drop (p + 1)) else none

def run : List (Edit α) → List α → Option (List α)
  | [], s => some s
  | e :: es, s => (e.apply s).bind (run es)

theorem ins_get (s : List α) (p : Nat) (c : α) (hp : p ≤ s.length) :
    (s.take p ++ c :: s.drop p)[p]? = some c := by
  have hl : (s.take p).length = p := by simp; omega
  rw [List.getElem?_append_right (by omega), hl]; simp
theorem take_take_self (s : List α) (p : Nat) : (s.take p).take p = s.take p := by
  simp [List.take_take]
theorem ins_del (s : List α) (p : Nat) (c : α) (hp : p ≤ s.length) :
    (s.take p ++ c :: s.drop p).take p ++ (s.take p ++ c :: s.drop p).drop (p + 1) = s := by
  have hl : (s.take p).length = p := by simp; omega
  rw [List.take_append, List.drop_append, hl]
  simp [List.drop_eq_nil_of_le (show (s.take p).length ≤ p + 1 by omega), take_take_self]
theorem del_ins (s : List α) (p : Nat) (c : α) (h : s[p]? = some c) :
    (s.take p ++ s.drop (p + 1)).take p ++ c :: (s.take p ++ s.drop (p + 1)).drop p = s := by
  have hp : p < s.length := by
    rcases Nat.lt_or_ge p s.length with h' | h'
    · exact h'
    · simp [List.getElem?_eq_none h'] at h
  have hl : (s.take p).length = p := by simp; omega
  rw [List.take_append, List.drop_append, hl]
  simp [take_take_self]
  conv => rhs; rw [← List.take_append_drop p s]
  congr 1
  rw [List.drop_eq_getElem_cons hp]
  simp_all

theorem apply_inv (e : Edit α) (s t : List α) (h : e.apply s = some t) :
    e.inv.apply t = some s := by
  cases e with
  | ins p c =>
    simp only [Edit.apply] at h
    split at h
    · rename_i hp
      cases h
      simp only [Edit.inv, Edit.apply, ins_get s p c hp, ite_true, ins_del s p c hp]
    · cases h
  | del p c =>
    simp only [Edit.apply] at h
    split at h
    · rename_i hc
      cases h
      have hp : p < s.length := by
        rcases Nat.lt_or_ge p s.length with h' | h'
        · exact h'
        · simp [List.getElem?_eq_none h'] at hc
      have hlen : p ≤ (s.take p ++ s.drop (p + 1)).length := by simp; omega
      simp only [Edit.inv, Edit.apply, hlen, ite_true, del_ins s p c hc]
    · cases h

theorem run_append (a b : List (Edit α)) (s : List α) :
    run (a ++ b) s = (run a s).bind (run b) := by
  induction a generalizing s with
  | nil => rfl
  | cons e es ih =>
    simp only [List.cons_append, run]
    cases e.apply s <;> simp [ih]

theorem undo_group (es : List (Edit α)) (s t : List α) (h : run es s = some t) :
    run (es.reverse.map Edit.inv) t = some s := by
  induction es generalizing s with
  | nil => simp [run] at h ⊢; exact h.symm
  | cons e es ih =>
    simp only [run] at h
    cases hu : e.apply s with
    | none => simp [hu] at h
    | some u =>
      simp [hu] at h
      simp only [List.reverse_cons, List.map_append, run_append, ih u h]
      simp [run, apply_inv e s u hu]

theorem jump (up down : List (Edit α)) (a c n : List α)
    (hc : run up a = some c) (hn : run down a = some n) :
    run (up.reverse.map Edit.inv ++ down) c = some n := by
  rw [run_append, undo_group up a c hc]; exact hn
