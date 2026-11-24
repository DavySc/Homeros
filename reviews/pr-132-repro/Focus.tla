------------------------------- MODULE Focus -------------------------------
(***************************************************************************)
(* Review of PR 132: the side of frames that Frames.tla leaves out.       *)
(* Which frame does a key act on?  The user types keys and SPC 5 o (next  *)
(* frame); the compositor delivers each key to the window that has the    *)
(* keyboard focus at that moment, and moves the focus when it handles the *)
(* editor's activation request, some time after the editor sent it (or    *)
(* never: Hyprland without focus_on_activate, research/frames-os.md 2).   *)
(*                                                                         *)
(* Property, before the mechanism:                                         *)
(*  InFrame  every key acts on the frame the user's own commands chose    *)
(*           before it (type-ahead after SPC 5 o goes to the new frame)   *)
(*                                                                         *)
(* Rules for the main loop (Variant):                                      *)
(*  "enter"   frames/header-request.md: SPC 5 o makes the next tab page   *)
(*            current at once and asks for the focus; a focus event makes *)
(*            its tab page current                                         *)
(*  "follow"  a key acts on the tab page of the window it arrived in      *)
(*            (backend.h's input() win[i])                                 *)
(*  "wait"    SPC 5 o only asks for the focus; the focus event enters     *)
(*  "count"   "enter", but a focus event that answers one of the editor's *)
(*            own requests is dropped (a sketch, to show InFrame can hold; *)
(*            it miscounts when the compositor refuses a request)          *)
(***************************************************************************)
EXTENDS Naturals, Sequences
CONSTANTS N, Budget, Rule, Variant
Frames == 1..N
Nxt(f) == IF f = N THEN 1 ELSE f + 1

VARIABLES intent, osf, wire, acts, cur, mine, wrong, used
vars == <<intent, osf, wire, acts, cur, mine, wrong, used>>

Init == intent = 1 /\ osf = 1 /\ wire = <<>> /\ acts = <<>> /\ cur = 1
        /\ mine = 0 /\ wrong = FALSE /\ used = 0

\* The user: a plain key, or SPC 5 o.  Both go to the window that has the focus now.
UKey == /\ used < Budget /\ used' = used + 1
        /\ wire' = Append(wire, [k |-> "key", win |-> osf, want |-> intent])
        /\ UNCHANGED <<intent, osf, acts, cur, mine, wrong>>
USwitch == /\ used < Budget /\ used' = used + 1
           /\ intent' = Nxt(intent)
           /\ wire' = Append(wire, [k |-> "sw", win |-> osf, want |-> Nxt(intent)])
           /\ UNCHANGED <<osf, acts, cur, mine, wrong>>

\* The compositor handles one activation request: grants it, or (without the rule) not.
Activate == /\ acts /= <<>> /\ acts' = Tail(acts)
            /\ \/ /\ osf' = Head(acts)
                  /\ wire' = IF Head(acts) /= osf
                             THEN Append(wire, [k |-> "focus", win |-> Head(acts), want |-> 0])
                             ELSE wire
               \/ ~Rule /\ UNCHANGED <<osf, wire>>
            /\ UNCHANGED <<intent, cur, mine, wrong, used>>

\* The editor takes one input event.
Process ==
  /\ wire /= <<>> /\ wire' = Tail(wire)
  /\ LET e == Head(wire)
         at == IF Variant = "follow" /\ e.k /= "focus" THEN e.win ELSE cur
     IN CASE e.k = "key" ->
               /\ wrong' = (wrong \/ at /= e.want) /\ cur' = at
               /\ UNCHANGED <<acts, mine>>
          [] e.k = "sw" ->
               /\ wrong' = (wrong \/ Nxt(at) /= e.want)
               /\ acts' = Append(acts, Nxt(at))
               /\ cur' = IF Variant = "wait" THEN at ELSE Nxt(at)
               /\ mine' = mine + 1
          [] OTHER ->
               /\ cur' = IF Variant = "count" /\ mine > 0 THEN cur ELSE e.win
               /\ mine' = IF mine > 0 THEN mine - 1 ELSE 0
               /\ UNCHANGED <<acts, wrong>>
  /\ UNCHANGED <<intent, osf, used>>

Next == UKey \/ USwitch \/ Activate \/ Process
Spec == Init /\ [][Next]_vars /\ WF_vars(Activate) /\ WF_vars(Process)

InFrame == ~wrong
\* With the rule, the editor and the system end up agreeing on the frame.
Agree == <>[](used = Budget => cur = osf)
=============================================================================
