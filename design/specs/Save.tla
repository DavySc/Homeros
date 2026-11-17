-------------------------------- MODULE Save --------------------------------
(***************************************************************************)
(* Saving a buffer while other programs (git checkout, a formatter, a     *)
(* second editor) may write the same file, and the machine may crash.      *)
(*                                                                         *)
(* Properties, stated before the mechanism:                               *)
(*  WholeFile    the file on disk is always one complete version, never a *)
(*               truncated or half-written one, even after a crash;        *)
(*  NoSilentLoss an external change that landed before the user pressed   *)
(*               save is never overwritten without the user being asked.   *)
(*  NoLossAtAll  (stronger, expected to FAIL even for "ok") no external    *)
(*               change is ever overwritten unasked.  Without a lock that  *)
(*               other programs honour, the window between check and       *)
(*               rename cannot be closed; TLC shows the trace.             *)
(*                                                                         *)
(* Mechanism (Variant = "ok"): on save, compare the file's identity       *)
(* (inode, size, mtime_ns; here a generation number) with the one seen at *)
(* load/last save; if different, ask.  Write a temp file, fsync, rename    *)
(* over the original.                                                      *)
(* Broken variants, each must fail TLC:                                    *)
(*  "nocheck"   write without comparing                                    *)
(*  "inplace"   truncate and write the file in place (crash -> torn file)  *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS MaxExt, Variant

\* disk: [gen, whole] ; gen counts writes; whole = FALSE means torn content.
VARIABLES disk, seen, pc, ext, extGens, asked, lostEarly, lostAny
vars == <<disk, seen, pc, ext, extGens, asked, lostEarly, lostAny>>

Init == /\ disk = [gen |-> 0, whole |-> TRUE] /\ seen = 0 /\ pc = "idle"
        /\ ext = 0 /\ extGens = {} /\ asked = FALSE
        /\ lostEarly = FALSE /\ lostAny = FALSE

\* Another program replaces the file (atomically, by rename).
ExtWrite ==
  /\ ext < MaxExt /\ disk.whole
  /\ disk' = [gen |-> disk.gen + 1, whole |-> TRUE]
  /\ ext' = ext + 1
  \* remember whether this write happened before or during our save
  /\ extGens' = extGens \cup {[g |-> disk.gen + 1, early |-> pc = "idle"]}
  /\ UNCHANGED <<seen, pc, asked, lostEarly, lostAny>>

StartSave == pc = "idle" /\ pc' = "check" /\ asked' = FALSE
             /\ UNCHANGED <<disk, seen, ext, extGens, lostEarly, lostAny>>

Check ==
  /\ pc = "check"
  /\ IF Variant /= "nocheck" /\ disk.gen /= seen
       THEN asked' = TRUE        \* user is asked; in this model they say "overwrite"
       ELSE asked' = asked
  /\ pc' = IF Variant = "inplace" THEN "trunc" ELSE "tmp"
  /\ UNCHANGED <<disk, seen, ext, extGens, lostEarly, lostAny>>

\* Overwriting generation disk.gen: was it an external version nobody saw?
Overwrite(newDisk) ==
  /\ disk' = newDisk
  \* a version the editor already loaded (seen) is not lost by overwriting it
  /\ LET victim == {e \in extGens : e.g = disk.gen /\ e.g /= seen} IN
       /\ lostAny'   = (lostAny   \/ (victim /= {} /\ ~asked))
       /\ lostEarly' = (lostEarly \/ (\E e \in victim : e.early /\ ~asked))

WriteTmp == pc = "tmp" /\ pc' = "rename"
            /\ UNCHANGED <<disk, seen, ext, extGens, asked, lostEarly, lostAny>>

Rename == pc = "rename"
          /\ Overwrite([gen |-> disk.gen + 1, whole |-> TRUE])
          /\ seen' = disk.gen + 1 /\ pc' = "idle"
          /\ UNCHANGED <<ext, extGens, asked>>

Trunc == pc = "trunc"
         /\ Overwrite([gen |-> disk.gen + 1, whole |-> FALSE])
         /\ pc' = "fill" /\ UNCHANGED <<seen, ext, extGens, asked>>

Fill == pc = "fill" /\ disk' = [disk EXCEPT !.whole = TRUE] /\ seen' = disk.gen
        /\ pc' = "idle" /\ UNCHANGED <<ext, extGens, asked, lostEarly, lostAny>>

\* Power loss: the save in progress is abandoned; disk keeps what it has.
Crash == pc /= "idle" /\ pc' = "idle"
         /\ UNCHANGED <<disk, seen, ext, extGens, asked, lostEarly, lostAny>>

\* The user reloads after being told the file changed.
Reload == pc = "idle" /\ disk.whole /\ seen /= disk.gen /\ seen' = disk.gen
          /\ UNCHANGED <<disk, pc, ext, extGens, asked, lostEarly, lostAny>>

Next == ExtWrite \/ StartSave \/ Check \/ WriteTmp \/ Rename \/ Trunc \/ Fill
        \/ Crash \/ Reload
Spec == Init /\ [][Next]_vars

\* Bound saves through the generation counter.
Bound == disk.gen <= 2 * MaxExt + 2

WholeFile    == disk.whole \/ pc = "fill"   \* torn only while we are writing
WholeAfterCrash == pc = "idle" => disk.whole
NoSilentLoss == ~lostEarly
NoLossAtAll  == ~lostAny
=============================================================================
