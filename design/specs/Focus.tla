------------------------------- MODULE Focus -------------------------------
(***************************************************************************)
(* Which frame a key acts on (frames thread; PR 132's review found the    *)
(* first rule wrong).  The user types keys, SPC 5 o (next frame) and      *)
(* clicks a frame; the compositor delivers each key to the window that    *)
(* has the keyboard focus at that moment, and moves the focus when it     *)
(* handles the editor's activation request, some time after the editor   *)
(* sent it, or never (Hyprland without focus_on_activate,                 *)
(* research/frames-os.md 2).  Requests are handled in the order sent.     *)
(*                                                                         *)
(* Properties, stated before the mechanism:                               *)
(*  InFrame  every key acts on the frame the user chose before it: by     *)
(*           SPC 5 o (type-ahead goes to the new frame), by a click, or   *)
(*           by the compositor raising a frame                            *)
(*  Agree    with the requests granted, the editor and the system end up  *)
(*           on the same frame                                            *)
(*                                                                         *)
(* Assumption: a click comes after the switches typed before it have      *)
(* happened: the editor has read them and the compositor has handled its  *)
(* requests (both take about a millisecond).  Inside that window the      *)
(* compositor itself undoes the click when it grants a later request, so  *)
(* no rule in the editor can follow the user there.                       *)
(*                                                                         *)
(* Mechanism (Variant "asked"): SPC 5 o enters the next tab page at once, *)
(* asks for the focus and then sends wl_display.sync; the frame joins     *)
(* asked (oldest first) and becomes want.  The compositor handles         *)
(* requests in order, so the sync's done comes after it has granted or    *)
(* refused that request: done drops the oldest entry of asked.  A focus   *)
(* event for want enters it; one for another frame still in asked        *)
(* answers an earlier request and is dropped; any other is the user's and *)
(* enters its tab page.  On Windows SetForegroundWindow answers inside    *)
(* the call, which is the interleaving with no event in between.          *)
(* Hyprland 0.57.2 focuses inside its activate handler                    *)
(* (XDGActivation.cpp setActivate, Window.cpp CWindow::activate), so the  *)
(* keyboard enter comes before the sync's done: read; to measure on sway  *)
(* and Davy's laptop with the wl backend.                                  *)
(* Broken variants, each must fail TLC:                                    *)
(*  "enter"   every focus event enters its tab page (the header request's *)
(*            rule; fails InFrame after two quick SPC 5 o)                 *)
(*  "follow"  a key acts on the tab page of the window it arrived in      *)
(*            (fails InFrame on type-ahead)                                *)
(*  "wait"    SPC 5 o only asks; the focus event enters (fails the same)  *)
(*  "nodone"  "asked" without the sync: a refused request stays in asked, *)
(*            and a later click on that frame is dropped (fails InFrame   *)
(*            when the compositor refuses, Hyprland without the rule)     *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets
In(f, s) == \E i \in 1..Len(s) : s[i] = f
CONSTANTS N, Budget, Rule, Variant
Frames == 1..N
Nxt(f) == IF f = N THEN 1 ELSE f + 1

VARIABLES intent, osf, wire, acts, cur, asked, want, wrong, used
vars == <<intent, osf, wire, acts, cur, asked, want, wrong, used>>

Init == intent = 1 /\ osf = 1 /\ wire = <<>> /\ acts = <<>> /\ cur = 1
        /\ asked = <<>> /\ want = 0 /\ wrong = FALSE /\ used = 0

Focus(f) == [k |-> "focus", win |-> f, want |-> 0]
\* the sync's done: only the mechanism asks for it
Done == IF Variant = "asked" THEN <<[k |-> "done", win |-> 0, want |-> 0]>> ELSE <<>>

\* The user: a plain key or SPC 5 o, both sent to the focused window; or a
\* click on another frame, which the compositor focuses at once.
UKey == /\ used < Budget /\ used' = used + 1
        /\ wire' = Append(wire, [k |-> "key", win |-> osf, want |-> intent])
        /\ UNCHANGED <<intent, osf, acts, cur, asked, want, wrong>>
USwitch == /\ used < Budget /\ used' = used + 1
           /\ intent' = Nxt(intent)
           /\ wire' = Append(wire, [k |-> "sw", win |-> osf, want |-> Nxt(intent)])
           /\ UNCHANGED <<osf, acts, cur, asked, want, wrong>>
UClick == /\ used < Budget /\ used' = used + 1
          /\ acts = <<>> /\ \A i \in 1..Len(wire) : wire[i].k /= "sw"
          /\ \E f \in Frames \ {osf} :
               intent' = f /\ osf' = f /\ wire' = Append(wire, Focus(f))
          /\ UNCHANGED <<acts, cur, asked, want, wrong>>

\* The compositor handles the oldest activation request: grants it (the
\* user now sees that frame focused), or, without the rule, only marks it.
Activate == /\ acts /= <<>> /\ acts' = Tail(acts)
            /\ \/ /\ osf' = Head(acts)
                  /\ wire' = (IF Head(acts) /= osf THEN Append(wire, Focus(Head(acts))) ELSE wire) \o Done
               \/ ~Rule /\ wire' = wire \o Done /\ UNCHANGED osf
            /\ UNCHANGED <<intent, cur, asked, want, wrong, used>>

\* The editor takes one input event.
Process ==
  /\ wire /= <<>> /\ wire' = Tail(wire)
  /\ LET e == Head(wire)
         at == IF Variant = "follow" /\ e.k /= "focus" THEN e.win ELSE cur
     IN CASE e.k = "key" ->
               /\ wrong' = (wrong \/ at /= e.want) /\ cur' = at
               /\ UNCHANGED <<acts, asked, want>>
          [] e.k = "sw" ->
               /\ wrong' = (wrong \/ Nxt(at) /= e.want)
               /\ acts' = Append(acts, Nxt(at))
               /\ cur' = IF Variant = "wait" THEN at ELSE Nxt(at)
               /\ asked' = Append(asked, Nxt(at)) /\ want' = Nxt(at)
          [] e.k = "done" ->
               /\ asked' = Tail(asked)
               /\ UNCHANGED <<acts, wrong, cur, want>>
          [] OTHER ->
               /\ cur' = IF Variant \in {"asked", "nodone"} /\ e.win /= want /\ In(e.win, asked) THEN cur ELSE e.win
               /\ UNCHANGED <<acts, wrong, asked, want>>
  /\ UNCHANGED <<intent, osf, used>>

Next == UKey \/ USwitch \/ UClick \/ Activate \/ Process
Spec == Init /\ [][Next]_vars /\ WF_vars(Activate) /\ WF_vars(Process)

InFrame == ~wrong
Agree == <>[](used = Budget => cur = osf)
=============================================================================
