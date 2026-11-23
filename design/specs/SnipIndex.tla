----------------------------- MODULE SnipIndex -----------------------------
(***************************************************************************)
(* SQL completion's file index (src/snip/, PR 141), 2026-11-23.  The       *)
(* popup's file-wide names come from one walk over the file (the index);   *)
(* the walk costs 26 to 33 ms at 700 KB, so it is made again on a          *)
(* loop_timer 150 ms after the last change (Neovim's LSP                   *)
(* debounce_text_changes), not on a key.  Until then snip_edit moves the   *)
(* index's statements with each edit (extmarks), and a word start walks    *)
(* its own statement again; a word start in a buffer without an index      *)
(* makes one at once.  The popup's words live in the index's pool, so the  *)
(* timer must not remake it while the popup is open.  Fire says what the   *)
(* timer does then:                                                        *)
(*   "ok"        the popup is open: arm again; else index the current     *)
(*               buffer if it is not the index's or changed since         *)
(*   "drop"      the popup is open: do nothing                            *)
(*   "scanopen"  index even with the popup open                           *)
(*   "onlyidx"   index only the index's own buffer (the first draft)      *)
(* and Marks what the index's statements do on an edit:                    *)
(*   "splice"    move with it (snip_edit); a word start indexes again if  *)
(*               they no longer match the text (its length, I.len)        *)
(*   "none"      stay, and a word start trusts an index of its buffer     *)
(* Properties, stated before the mechanism:                                *)
(*   Safe   while the popup is open its words are the pool's (no walk     *)
(*          moved them)                                                   *)
(*   Right  a word start reads the index of its own buffer, with          *)
(*          statements that follow every edit of it                       *)
(*   Live   an edit not yet indexed is indexed, unless the popup stays    *)
(*          open or the buffer is left (keys there arm the timer again)   *)
(* Claims: "ok" with "splice" has all three; "drop" and "onlyidx" break   *)
(* Live; "scanopen" breaks Safe; "none" breaks Right.                      *)
(***************************************************************************)
EXTENDS Integers
CONSTANTS Fire, Marks, MaxActs
VARIABLES cur, txt, idx, armed, pum, itemsOk, dirty, right, acts
vars == <<cur, txt, idx, armed, pum, itemsOk, dirty, right, acts>>

Bufs == {1, 2}
\* idx.b: its buffer (0 none); idx.v: the text it walked; idx.m: the text
\* its statement marks follow (snip_edit)
Init == /\ cur = 1 /\ txt = [b \in Bufs |-> 0] /\ idx = [b |-> 0, v |-> 0, m |-> 0]
        /\ armed = FALSE /\ pum = FALSE /\ itemsOk = TRUE /\ dirty = FALSE
        /\ right = TRUE /\ acts = 0

Scan == idx' = [b |-> cur, v |-> txt[cur], m |-> txt[cur]]

User == acts < MaxActs /\ acts' = acts + 1

\* a key that changes the text (a word's character keeps the popup)
Edit == /\ User /\ txt' = [txt EXCEPT ![cur] = @ + 1]
        /\ idx' = IF idx.b = cur /\ Marks = "splice" THEN [idx EXCEPT !.m = txt'[cur]] ELSE idx
        /\ armed' = TRUE /\ dirty' = TRUE
        /\ UNCHANGED <<cur, pum, itemsOk, right>>

\* :b, CTRL-^: the popup closes
Switch == /\ User /\ cur' = 3 - cur /\ pum' = FALSE /\ dirty' = FALSE
          /\ UNCHANGED <<txt, idx, armed, itemsOk, right>>

\* any other key: snip_key arms the timer in a buffer without the index;
\* the popup closes
Key == /\ User /\ pum' = FALSE
       /\ armed' = (armed \/ idx.b /= cur)
       /\ UNCHANGED <<cur, txt, idx, itemsOk, dirty, right>>

\* a word starts: collect(), indexing at once without an index for it
WordStart == /\ User /\ ~pum
             /\ IF idx.b /= cur \/ (Marks = "splice" /\ idx.m /= txt[cur]) THEN Scan ELSE UNCHANGED idx
             /\ right' = (idx'.b = cur /\ idx'.m = txt[cur])
             /\ pum' = TRUE /\ itemsOk' = TRUE
             /\ dirty' = (dirty /\ idx'.v /= txt[cur])
             /\ UNCHANGED <<cur, txt, armed>>

TimerFire == /\ armed /\ armed' = (pum /\ Fire = "ok")
             /\ IF pum
                THEN CASE Fire = "drop"     -> UNCHANGED <<idx, itemsOk, dirty>>
                       [] Fire = "scanopen" -> Scan /\ itemsOk' = FALSE /\ dirty' = FALSE
                       [] OTHER             -> UNCHANGED <<idx, itemsOk, dirty>>
                ELSE IF (Fire = "onlyidx" /\ idx.b = cur /\ idx.v /= txt[cur])
                        \/ (Fire /= "onlyidx" /\ (idx.b /= cur \/ idx.v /= txt[cur]))
                     THEN Scan /\ dirty' = FALSE /\ UNCHANGED itemsOk
                     ELSE UNCHANGED <<idx, itemsOk, dirty>>
             /\ UNCHANGED <<cur, txt, pum, right, acts>>

Next == Edit \/ Switch \/ Key \/ WordStart \/ TimerFire
Spec == Init /\ [][Next]_vars /\ WF_vars(TimerFire)

Safe  == pum => itemsOk
Right == right
Live  == dirty ~> (~dirty \/ pum)
=============================================================================
