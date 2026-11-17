-------------------------------- MODULE Break -------------------------------
(***************************************************************************)
(* Ctrl-C during an endless command (Neovim stops `:while 1 | endwhile`,   *)
(* Helix ignores Ctrl-C during a 100-million-repeat macro: REVIEW1 #6).    *)
(* The user types keys at any rate into the terminal's buffer; the editor  *)
(* holds keys it has read in a queue of Q until a command takes them.      *)
(*                                                                         *)
(* Property, stated before the mechanism:                                 *)
(*  Stops  once Ctrl-C is typed, the running command ends, however many    *)
(*         keys were typed before it.                                      *)
(*                                                                         *)
(* Mechanism (Variant = "ok"): the command calls loop_breakcheck, which    *)
(* runs the input's read function (Neovim's line_breakcheck); that         *)
(* function reads even when its queue is full, and a Ctrl-C sets loop_int  *)
(* and drops the keys typed before it (Vim's flush_buffers).               *)
(* Between commands the read function leaves input in the terminal while *)
(* its queue is full, so nothing is lost (PR 7's review: reading past a    *)
(* full queue there lost 26 KB of a 40 KB paste); that is why Read below   *)
(* runs only while the command does.                                       *)
(* Broken variants, each must fail TLC:                                    *)
(*  "nocheck"  input is read only between commands (Helix)                 *)
(*  "full"     the read function stops reading while its queue is full,    *)
(*             so a Ctrl-C behind Q keys stays unread                      *)
(***************************************************************************)
EXTENDS Naturals, Sequences

CONSTANTS Q, Typed, Variant

VARIABLES tty, q, typed, pressed, int, running
vars == <<tty, q, typed, pressed, int, running>>

Init == tty = <<>> /\ q = <<>> /\ typed = 0 /\ pressed = FALSE /\ int = FALSE /\ running = TRUE

\* The user: up to Typed keys, any of them may be the one Ctrl-C.
Type == typed < Typed /\ typed' = typed + 1
        /\ \E k \in {"k", "C"} : /\ k = "C" => ~pressed
                                 /\ tty' = Append(tty, k)
                                 /\ pressed' = (pressed \/ k = "C")
        /\ UNCHANGED <<q, int, running>>

\* loop_breakcheck inside the command runs the read function.
Read == /\ running /\ Variant /= "nocheck" /\ tty /= <<>>
        /\ Variant = "full" => Len(q) < Q
        /\ tty' = Tail(tty)
        /\ IF Head(tty) = "C" THEN int' = TRUE /\ q' = <<>>
           ELSE int' = int /\ q' = IF Len(q) < Q THEN Append(q, Head(tty)) ELSE q
        /\ UNCHANGED <<typed, pressed, running>>

\* The endless command ends only when it sees loop_int.
Stop == running /\ int /\ running' = FALSE /\ UNCHANGED <<tty, q, typed, pressed, int>>

Next == Type \/ Read \/ Stop
Spec == Init /\ [][Next]_vars /\ WF_vars(Read) /\ WF_vars(Stop)

Stops == pressed ~> ~running
=============================================================================
