------------------------------- MODULE Save2 -------------------------------
(* Review model.  Two things Save.tla abstracts away:
   1. durability: a rename can reach the disk before the temp file's data
      (page cache), so "write temp, rename" without fsync leaves an empty
      file after power loss.  Variant "nofsync" vs "fsync".
   2. the claim "no editor can close the check-to-rename window without a
      lock": Variant "exchange" swaps temp and target atomically
      (renameat2 RENAME_EXCHANGE), then looks at what it swapped out.  If
      that is not the version it loaded, the outside version is still on
      disk under the temp name and the user is asked.  *)
EXTENDS Naturals
CONSTANTS MaxExt, Variant
VARIABLES name,     \* what the file name points at: [gen, data \in {"durable","cached"}]
          seen, pc, ext, asked, lostAny, crashed, kept
vars == <<name, seen, pc, ext, asked, lostAny, crashed, kept>>

Init == /\ name = [gen |-> 0, data |-> "durable"] /\ seen = 0 /\ pc = "idle"
        /\ ext = 0 /\ asked = FALSE /\ lostAny = FALSE /\ crashed = FALSE /\ kept = FALSE

ExtWrite == /\ ~crashed /\ ext < MaxExt
            /\ name' = [gen |-> name.gen + 1, data |-> "durable"]
            /\ ext' = ext + 1 /\ UNCHANGED <<seen, pc, asked, lostAny, crashed, kept>>
StartSave == ~crashed /\ pc = "idle" /\ pc' = "check" /\ asked' = FALSE /\ kept' = FALSE
             /\ UNCHANGED <<name, seen, ext, lostAny, crashed>>
Check == /\ ~crashed /\ pc = "check"
         /\ asked' = (name.gen /= seen)
         /\ pc' = "rename" /\ UNCHANGED <<name, seen, ext, lostAny, crashed, kept>>
Rename ==
  /\ ~crashed /\ pc = "rename"
  /\ LET unseen == name.gen /= seen /\ ~asked IN
     IF Variant = "exchange" /\ unseen
       THEN \* swapped-out file is not ours: it is kept under the temp name, user is asked
            /\ kept' = TRUE /\ lostAny' = lostAny
       ELSE /\ kept' = kept /\ lostAny' = (lostAny \/ unseen)
  /\ name' = [gen |-> name.gen + 1, data |-> IF Variant = "nofsync" THEN "cached" ELSE "durable"]
  /\ seen' = name.gen + 1 /\ pc' = "idle"
  /\ UNCHANGED <<ext, asked, crashed>>
\* The kernel writes cached data back at some point.
Flush == ~crashed /\ name.data = "cached" /\ name' = [name EXCEPT !.data = "durable"]
         /\ UNCHANGED <<seen, pc, ext, asked, lostAny, crashed, kept>>
\* Power loss.  Metadata (the rename) is journalled; cached data is gone.
Crash == ~crashed /\ crashed' = TRUE /\ UNCHANGED <<name, seen, pc, ext, asked, lostAny, kept>>
Next == ExtWrite \/ StartSave \/ Check \/ Rename \/ Flush \/ Crash
Spec == Init /\ [][Next]_vars
Bound == name.gen <= 2 * MaxExt + 2
WholeAfterCrash == crashed => name.data = "durable"
NoLossAtAll == ~lostAny
=============================================================================
