------------------------------- MODULE WinWait -------------------------------
(***************************************************************************)
(* The infoview's child on Windows (src/infoview/serve/serve.c, io_wait   *)
(* and the send loop) with one browser connection.  The browser sends R   *)
(* requests, one at a time as it likes, and reads the answers whenever it *)
(* likes; each answer is A bytes, more than the socket's send buffer      *)
(* (Cap) holds, so sends must wait for the browser.  The pipes to the      *)
(* editor are left out: their completion routines wake the wait directly. *)
(*                                                                         *)
(* Property, stated before the mechanism:                                 *)
(*  Done  every answer reaches the browser (the loop never sleeps while a *)
(*        request or an answer waits for it).                             *)
(*                                                                         *)
(* Winsock, as documented (WSAEventSelect, send):                         *)
(*  - the event is manual-reset, set by FD_READ and FD_WRITE;             *)
(*  - FD_READ is posted when data arrives and FD_READ is enabled; recv    *)
(*    re-enables it, and posts it again at once if data is left;          *)
(*  - FD_WRITE is posted when there is room again, but only after a send  *)
(*    failed with WSAEWOULDBLOCK;                                         *)
(*  - a non-blocking send takes some or all of what is offered while the  *)
(*    buffer has room (Partial = FALSE: all of it, which is what Windows  *)
(*    did in every run measured, PR 138's review), else WSAEWOULDBLOCK.   *)
(*                                                                         *)
(* Mechanism (Order = "reset", Sends = "loop", FdWrite = TRUE): wait for  *)
(* the event; reset it; WSAPoll(0) says which sockets are readable; recv  *)
(* one; send until done or WSAEWOULDBLOCK.                                *)
(* Broken variants, must fail TLC:                                        *)
(*  "late"     the event reset after the poll: a request that comes in    *)
(*             between is never seen                                      *)
(*  "once"     one send per turn (Partial): a short send arms no FD_WRITE  *)
(*  "nowrite"  FD_WRITE not in the event's mask                           *)
(* And one that passes: "once" when sends are all-or-nothing (Partial =   *)
(* FALSE): the send loop matters only for a short send.                   *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS R, A, Cap, Order, Sends, FdWrite, Partial

VARIABLES pc, ev, rArm, wArm, req, asked, seen, out, buf, got
vars == <<pc, ev, rArm, wArm, req, asked, seen, out, buf, got>>

Init == pc = "wait" /\ ev = FALSE /\ rArm = TRUE /\ wArm = FALSE /\ req = 0
        /\ asked = 0 /\ seen = FALSE /\ out = 0 /\ buf = 0 /\ got = 0

(* the browser *)
Ask  == asked < R /\ asked' = asked + 1 /\ req' = req + 1
        /\ ev' = (ev \/ rArm) /\ rArm' = FALSE
        /\ UNCHANGED <<pc, wArm, seen, out, buf, got>>
Read == buf > 0 /\ buf' = buf - 1 /\ got' = got + 1
        /\ ev' = (ev \/ (wArm /\ FdWrite)) /\ wArm' = (wArm /\ ~FdWrite)
        /\ UNCHANGED <<pc, rArm, req, asked, seen, out>>

(* the child *)
Wake  == pc = "wait" /\ ev /\ pc' = (IF Order = "reset" THEN "reset" ELSE "poll")
         /\ UNCHANGED <<ev, rArm, wArm, req, asked, seen, out, buf, got>>
Reset == pc = "reset" /\ ev' = FALSE /\ pc' = (IF Order = "reset" THEN "poll" ELSE "recv")
         /\ UNCHANGED <<rArm, wArm, req, asked, seen, out, buf, got>>
Poll  == pc = "poll" /\ seen' = (req > 0) /\ pc' = (IF Order = "reset" THEN "recv" ELSE "reset")
         /\ UNCHANGED <<ev, rArm, wArm, req, asked, out, buf, got>>
Recv  == pc = "recv" /\ pc' = "send" /\ seen' = FALSE
         /\ (IF seen
             THEN req' = req - 1 /\ out' = out + A
                  /\ ev' = (ev \/ req > 1) /\ rArm' = (req = 1)
             ELSE UNCHANGED <<req, out, ev, rArm>>)
         /\ UNCHANGED <<wArm, asked, buf, got>>
Send  == pc = "send"
         /\ (IF out = 0 THEN pc' = "wait" /\ UNCHANGED <<wArm, out, buf>>
             ELSE IF buf >= Cap THEN pc' = "wait" /\ wArm' = TRUE /\ UNCHANGED <<out, buf>>
             ELSE \E k \in (IF Partial THEN 1..out ELSE {out}):
                  out' = out - k /\ buf' = buf + k /\ UNCHANGED wArm
                  /\ pc' = (IF Sends = "loop" THEN "send" ELSE "wait"))
         /\ UNCHANGED <<ev, rArm, req, asked, seen, got>>

Next == Ask \/ Read \/ Wake \/ Reset \/ Poll \/ Recv \/ Send
Spec == Init /\ [][Next]_vars /\ WF_vars(Ask) /\ WF_vars(Read) /\ WF_vars(Wake)
        /\ WF_vars(Reset) /\ WF_vars(Poll) /\ WF_vars(Recv) /\ WF_vars(Send)

Done == <>(got = R * A)
=============================================================================
