-------------------------------- MODULE Wake --------------------------------
(***************************************************************************)
(* Package 22 on Windows: one SQL connection's socket in the event loop    *)
(* (src/sql/net.c).  WSAEventSelect stands for the socket with one event,  *)
(* which the loop waits on; Winsock records FD_READ, FD_WRITE and          *)
(* FD_CLOSE and sets the event; WSAEnumNetworkEvents hands the records     *)
(* over, clears them and resets the event.  A record is made again only    *)
(* once re-enabled: FD_READ by a recv (at once if data is still there),   *)
(* FD_WRITE by a send that would block, FD_CLOSE never (measured on        *)
(* Windows Server 2025 in the review of PR 130: 15 of 18 runs never        *)
(* signalled again after the close came with the last bytes; Wine does).   *)
(* The server sends N chunks then closes (Close), or reads the M units the *)
(* client queued through a socket buffer of B units.  Each wake the        *)
(* client runs tds.c's and pg.c's io: net_flush, then net_read (woke,      *)
(* then in "ok" net_flush again, then one recv).                           *)
(*                                                                         *)
(* Properties, stated before the mechanism:                               *)
(*  ReadsClose  a close the server sent is read in the end (recv says 0); *)
(*  SendsAll    the queued bytes are sent in the end.                      *)
(*                                                                         *)
(* Mechanism (Variant = "ok"): woke keeps the event set once FD_CLOSE      *)
(* came, until the recv that says 0; net_read flushes after woke.          *)
(* Broken variants, must fail TLC:                                         *)
(*  "noclose"     the event is reset with the close's record: its last     *)
(*                bytes read, nothing wakes the client again;              *)
(*  "flushfirst"  no flush after woke: the socket turns writable between   *)
(*                io's flush and the reset, which takes FD_WRITE with it.  *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS N, M, B, Close, Variant

VARIABLES ev, rec, readEn, writeEn, avail, sent, fin, q, room, pc, shut, done
vars == <<ev, rec, readEn, writeEn, avail, sent, fin, q, room, pc, shut, done>>

Init == ev = FALSE /\ rec = {} /\ readEn = TRUE /\ writeEn = (M > 0) /\ avail = 0 /\ sent = 0
        /\ fin = FALSE /\ q = M /\ room = 0 /\ pc = "idle" /\ shut = FALSE /\ done = FALSE

(* the server and the network *)
Arrive == Close /\ sent < N /\ sent' = sent + 1 /\ avail' = avail + 1
          /\ (IF readEn THEN rec' = rec \cup {"read"} /\ ev' = TRUE /\ readEn' = FALSE
              ELSE UNCHANGED <<rec, ev, readEn>>)
          /\ UNCHANGED <<writeEn, fin, q, room, pc, shut, done>>
Fin == Close /\ sent = N /\ ~fin /\ fin' = TRUE /\ rec' = rec \cup {"close"} /\ ev' = TRUE
       /\ UNCHANGED <<readEn, writeEn, avail, sent, q, room, pc, shut, done>>
Drain == room < B /\ room' = room + 1
         /\ (IF writeEn THEN rec' = rec \cup {"write"} /\ ev' = TRUE /\ writeEn' = FALSE
             ELSE UNCHANGED <<rec, ev, writeEn>>)
         /\ UNCHANGED <<readEn, avail, sent, fin, q, pc, shut, done>>

(* net_flush: send until one would block, which re-enables FD_WRITE *)
Flush(next) == LET k == IF q < room THEN q ELSE room IN
               q' = q - k /\ room' = room - k /\ writeEn' = (writeEn \/ q - k > 0) /\ pc' = next
               /\ UNCHANGED <<ev, rec, readEn, avail, sent, fin, shut, done>>

(* the client *)
Wake == pc = "idle" /\ ev /\ ~done /\ pc' = "f1" /\ UNCHANGED <<ev, rec, readEn, writeEn, avail, sent, fin, q, room, shut, done>>
F1 == pc = "f1" /\ Flush("woke")
Woke == pc = "woke" /\ shut' = (shut \/ "close" \in rec) /\ rec' = {}
        /\ ev' = (Variant # "noclose" /\ shut')
        /\ pc' = (IF Variant = "flushfirst" THEN "recv" ELSE "f2")
        /\ UNCHANGED <<readEn, writeEn, avail, sent, fin, q, room, done>>
F2 == pc = "f2" /\ Flush("recv")
Recv == pc = "recv" /\ pc' = "idle"
        /\ (IF avail > 0 THEN avail' = avail - 1 /\ UNCHANGED done
                              /\ (IF avail > 1 THEN rec' = rec \cup {"read"} /\ ev' = TRUE /\ readEn' = FALSE
                                  ELSE readEn' = TRUE /\ UNCHANGED <<rec, ev>>)
            ELSE UNCHANGED <<avail, rec, ev>> /\ readEn' = TRUE /\ done' = fin)
        /\ UNCHANGED <<writeEn, sent, fin, q, room, shut>>

Next == Arrive \/ Fin \/ Drain \/ Wake \/ F1 \/ Woke \/ F2 \/ Recv
Spec == Init /\ [][Next]_vars /\ WF_vars(Arrive) /\ WF_vars(Fin) /\ WF_vars(Drain)
        /\ WF_vars(Wake) /\ WF_vars(F1) /\ WF_vars(Woke) /\ WF_vars(F2) /\ WF_vars(Recv)

ReadsClose == Close => <>done
SendsAll == <>(q = 0)
=============================================================================
