-------------------------------- MODULE Pipe --------------------------------
(***************************************************************************)
(* The editor and one child process on two pipes of finite capacity.  The *)
(* editor has M bytes to send (a didOpen); the child first writes its K    *)
(* bytes and reads only after that (a server publishing diagnostics before *)
(* it reads: design/reviews/r1/dead.py, the worst order for the editor).   *)
(*                                                                         *)
(* Property, stated before the mechanism:                                 *)
(*  Finish  every byte reaches the other side, whatever the sizes          *)
(*          (no deadlock, no stall).                                       *)
(*                                                                         *)
(* Mechanism (Variant = "queue"): the editor's writes are non-blocking;    *)
(* what the pipe does not take waits in an output queue, written when poll *)
(* reports room, and the editor keeps reading meanwhile (src/loop/).       *)
(* Broken variant, must fail TLC:                                          *)
(*  "block"  a blocking write: the editor reads only after all M bytes are *)
(*           in the pipe                                                   *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS M, K, Cap, Variant

VARIABLES eq, toC, cLeft, toE, eGot
vars == <<eq, toC, cLeft, toE, eGot>>

Init == eq = M /\ toC = 0 /\ cLeft = K /\ toE = 0 /\ eGot = 0

EWrite == eq > 0 /\ toC < Cap /\ eq' = eq - 1 /\ toC' = toC + 1 /\ UNCHANGED <<cLeft, toE, eGot>>
ERead  == toE > 0 /\ (Variant = "queue" \/ eq = 0)
          /\ toE' = toE - 1 /\ eGot' = eGot + 1 /\ UNCHANGED <<eq, toC, cLeft>>
CWrite == cLeft > 0 /\ toE < Cap /\ cLeft' = cLeft - 1 /\ toE' = toE + 1 /\ UNCHANGED <<eq, toC, eGot>>
CRead  == toC > 0 /\ cLeft = 0 /\ toC' = toC - 1 /\ UNCHANGED <<eq, cLeft, toE, eGot>>

Next == EWrite \/ ERead \/ CWrite \/ CRead
Spec == Init /\ [][Next]_vars /\ WF_vars(EWrite) /\ WF_vars(ERead) /\ WF_vars(CWrite) /\ WF_vars(CRead)

Finish == <>(eq = 0 /\ toC = 0 /\ cLeft = 0 /\ toE = 0 /\ eGot = K)
=============================================================================
