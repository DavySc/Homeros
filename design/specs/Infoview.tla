------------------------------- MODULE Infoview -------------------------------
(***************************************************************************)
(* VS Code's infoview in a browser page, the editor its editor, through   *)
(* homeros-infoview, a child process on a pipe (bridge.h, package 14).     *)
(* kittymax's test/kitty_lean_webview.tla with its one Emacs cut in two:   *)
(* the child holds the page's connection (its event stream, the outbox of  *)
(* answers that wait while the stream is down, the timeout that forgets   *)
(* it); the editor holds what the calls mean (its RPC sessions, kept alive *)
(* with Lean).  Between them two pipes, each in order, each with messages *)
(* in flight: a page can be forgotten by the child while its calls are    *)
(* still on the way to the editor, and Lean can answer a page the child   *)
(* has forgotten.                                                          *)
(*                                                                         *)
(* A page calls with POSTs (ids from 0); the answer comes later on its    *)
(* event stream.  The stream breaks and is reopened (EventSource does);   *)
(* the page can also go away for good, and is forgotten after a while.     *)
(* A forgotten page that comes back (a laptop asleep) is told to reload:  *)
(* a new page, its calls counted from 0 again (at most once here, to keep *)
(* the model finite, and so a loaded page is forgotten at most once).    *)
(*                                                                         *)
(* Properties, stated before the mechanism:                               *)
(*  OwnAnswers   a page gets answers to its own calls only.                *)
(*  NothingLeft  once the editor has heard that the child forgot a page,   *)
(*               it keeps nothing of it: no RPC sessions kept alive.       *)
(*  Answered     every call of a page that stays is answered (or the page *)
(*               reloaded, which starts its calls afresh).                 *)
(*                                                                         *)
(* Mechanism (Bug = "none", src/infoview/): the child drops a forgotten    *)
(* page's calls and answers ("- P" is the last line it sends about it);   *)
(* an answer for a page whose stream is down waits in its outbox; the     *)
(* editor keeps a page from its first call to the child's "- P" and drops *)
(* Lean's answers for a page it does not have.                             *)
(* Broken variants, each must fail TLC:                                    *)
(*  "create"     the editor makes the page again for a late answer (a     *)
(*               session for a forgotten page, kept alive for ever)        *)
(*  "nobox"      an answer for a page whose stream is down is dropped      *)
(*  "broadcast"  answers go to every open stream                           *)
(*  "noreload"   a forgotten page that comes back is not told to reload:   *)
(*               it carries on under an id the editor has dropped          *)
(* Ids = {0} is enough: a reloaded page calls 0 again, the id a late      *)
(* answer for its old self carries (with {0, 1}: no error, 2.7M states    *)
(* and more, stopped after 15 min).                                        *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets

CONSTANTS Pages, Ids, Bug

VARIABLES
  alive,     \* page -> still loaded
  mounted,   \* page -> the infoview runs (it calls; it reads its stream)
  called,    \* page -> ids it called (this incarnation)
  reloads,   \* page -> times it reloaded
  stream,    \* page -> "none", "open", "down"           (the child's)
  chas,      \* page -> the child has a record of it
  cforgot,   \* page -> the child forgot it (and drops its calls)
  outbox,    \* answers <<p, i>> waiting in the child for p's stream
  got,       \* answers the pages received: <<to, <<from, i>>>>
  toEd,      \* the child's stdout: <<"call", p, i>> or <<"forget", p, 0>>
  toCh,      \* the editor's stdout: answers <<p, i>>
  epage,     \* page -> the editor has it
  waiting,   \* calls Lean has not answered: <<p, i>>
  sessions   \* pages with an RPC session the editor keeps alive

vars == <<alive, mounted, called, reloads, stream, chas, cforgot, outbox, got, toEd, toCh,
          epage, waiting, sessions>>

Init == /\ alive = [p \in Pages |-> TRUE] /\ mounted = [p \in Pages |-> FALSE]
        /\ called = [p \in Pages |-> {}] /\ reloads = [p \in Pages |-> 0]
        /\ stream = [p \in Pages |-> "none"] /\ chas = [p \in Pages |-> FALSE]
        /\ cforgot = [p \in Pages |-> FALSE] /\ outbox = {} /\ got = {}
        /\ toEd = <<>> /\ toCh = <<>> /\ epage = [p \in Pages |-> FALSE]
        /\ waiting = {} /\ sessions = {}

\* a POST reaches the child: dropped if it forgot the page, else passed on
Post(p, i) == IF cforgot[p] THEN UNCHANGED <<toEd, chas>>
              ELSE /\ toEd' = Append(toEd, <<"call", p, i>>)
                   /\ chas' = [chas EXCEPT ![p] = TRUE]

\* the page loads and makes its first call (React's effects)
Mount(p) == /\ alive[p] /\ ~mounted[p]
            /\ mounted' = [mounted EXCEPT ![p] = TRUE]
            /\ called' = [called EXCEPT ![p] = @ \cup {0}]
            /\ Post(p, 0)
            /\ UNCHANGED <<alive, reloads, stream, cforgot, outbox, got, toCh, epage, waiting, sessions>>

Call(p, i) == /\ alive[p] /\ mounted[p] /\ i \notin called[p]
              /\ called' = [called EXCEPT ![p] = @ \cup {i}]
              /\ Post(p, i)
              /\ UNCHANGED <<alive, mounted, reloads, stream, cforgot, outbox, got, toCh, epage, waiting, sessions>>

\* the page (re)opens its event stream; the child sends what waited
Open(p) == /\ alive[p] /\ mounted[p] /\ stream[p] # "open"
           /\ (~cforgot[p] \/ Bug = "noreload")
           /\ stream' = [stream EXCEPT ![p] = "open"]
           /\ chas' = [chas EXCEPT ![p] = TRUE]
           /\ got' = got \cup {<<p, a>> : a \in {x \in outbox : x[1] = p}}
           /\ outbox' = {x \in outbox : x[1] # p}
           /\ UNCHANGED <<alive, mounted, called, reloads, cforgot, toEd, toCh, epage, waiting, sessions>>

\* a forgotten page that is still loaded comes back: told to reload, it is a
\* new page (the old id stays forgotten; the new one is p again here)
Reload(p) == /\ alive[p] /\ cforgot[p] /\ reloads[p] = 0 /\ Bug # "noreload"
             /\ reloads' = [reloads EXCEPT ![p] = 1]
             /\ cforgot' = [cforgot EXCEPT ![p] = FALSE]
             /\ mounted' = [mounted EXCEPT ![p] = FALSE]
             /\ called' = [called EXCEPT ![p] = {}]
             /\ stream' = [stream EXCEPT ![p] = "none"]
             /\ got' = {g \in got : g[1] # p}
             /\ UNCHANGED <<alive, chas, outbox, toEd, toCh, epage, waiting, sessions>>

Break(p) == /\ stream[p] = "open"
            /\ stream' = [stream EXCEPT ![p] = "down"]
            /\ UNCHANGED <<alive, mounted, called, reloads, chas, cforgot, outbox, got, toEd, toCh, epage, waiting, sessions>>

Leave(p) == /\ alive[p]
            /\ alive' = [alive EXCEPT ![p] = FALSE]
            /\ stream' = [stream EXCEPT ![p] = IF @ = "open" THEN "down" ELSE @]
            /\ UNCHANGED <<mounted, called, reloads, chas, cforgot, outbox, got, toEd, toCh, epage, waiting, sessions>>

\* the timeout: the child forgets a page whose stream is not open, and says so
Forget(p) == /\ chas[p] /\ stream[p] # "open" /\ (~alive[p] \/ reloads[p] = 0)
             /\ chas' = [chas EXCEPT ![p] = FALSE]
             /\ cforgot' = [cforgot EXCEPT ![p] = TRUE]
             /\ outbox' = {x \in outbox : x[1] # p}
             /\ toEd' = Append(toEd, <<"forget", p, 0>>)
             /\ UNCHANGED <<alive, mounted, called, reloads, stream, got, toCh, epage, waiting, sessions>>

\* the editor reads a line from the child
EdRead == /\ toEd # <<>>
          /\ LET m == Head(toEd) IN
             IF m[1] = "call"
             THEN /\ epage' = [epage EXCEPT ![m[2]] = TRUE]
                  /\ waiting' = waiting \cup {<<m[2], m[3]>>}
                  /\ UNCHANGED sessions
             ELSE /\ epage' = [epage EXCEPT ![m[2]] = FALSE]
                  /\ sessions' = sessions \ {m[2]}
                  /\ UNCHANGED waiting
          /\ toEd' = Tail(toEd)
          /\ UNCHANGED <<alive, mounted, called, reloads, stream, chas, cforgot, outbox, got, toCh>>

\* Lean answers a call (every call opens a session here, the worst case)
Answer(c) == /\ c \in waiting
             /\ waiting' = waiting \ {c}
             /\ IF epage[c[1]] \/ Bug = "create"
                THEN /\ epage' = [epage EXCEPT ![c[1]] = TRUE]
                     /\ sessions' = sessions \cup {c[1]}
                     /\ toCh' = Append(toCh, c)
                ELSE UNCHANGED <<epage, sessions, toCh>>
             /\ UNCHANGED <<alive, mounted, called, reloads, stream, chas, cforgot, outbox, got, toEd>>

\* the child reads an answer from the editor
ChRead == /\ toCh # <<>>
          /\ LET c == Head(toCh) IN
             IF ~chas[c[1]] THEN UNCHANGED <<got, outbox>>
             ELSE IF Bug = "broadcast"
             THEN /\ got' = got \cup {<<q, c>> : q \in {r \in Pages : stream[r] = "open"}}
                  /\ outbox' = IF stream[c[1]] = "open" THEN outbox ELSE outbox \cup {c}
             ELSE IF stream[c[1]] = "open" THEN /\ got' = got \cup {<<c[1], c>>} /\ UNCHANGED outbox
             ELSE IF Bug = "nobox" THEN UNCHANGED <<got, outbox>>
             ELSE /\ outbox' = outbox \cup {c} /\ UNCHANGED got
          /\ toCh' = Tail(toCh)
          /\ UNCHANGED <<alive, mounted, called, reloads, stream, chas, cforgot, toEd, epage, waiting, sessions>>

Next == \/ EdRead \/ ChRead \/ \E c \in waiting : Answer(c)
        \/ \E p \in Pages :
             \/ Mount(p) \/ Open(p) \/ Break(p) \/ Leave(p) \/ Forget(p) \/ Reload(p)
             \/ \E i \in Ids : Call(p, i)

\* pipes are read, Lean answers, pages load and reopen their streams, come-back
\* pages reload, a page gone for good is forgotten; a loaded page is not
\* forgotten for ever and again (nor its stream broken for ever)
Fair == /\ WF_vars(EdRead) /\ WF_vars(ChRead) /\ \A c \in Pages \X Ids : WF_vars(Answer(c))
        /\ \A p \in Pages : /\ WF_vars(Mount(p)) /\ WF_vars(Open(p)) /\ WF_vars(Reload(p))
                            /\ WF_vars(~alive[p] /\ Forget(p))

Spec == Init /\ [][Next]_vars /\ Fair

OwnAnswers == \A g \in got : g[2][1] = g[1]

\* the editor has heard "forget p" (no such line still in the pipe)
Told(p) == cforgot[p] /\ ~\E k \in 1..Len(toEd) : toEd[k] = <<"forget", p, 0>>
NothingLeft == \A p \in Pages :
                 (Told(p) /\ ~(\E k \in 1..Len(toEd) : toEd[k][2] = p))
                 => (p \notin sessions /\ ~epage[p] /\ ~(\E x \in outbox : x[1] = p))

Answered == \A p \in Pages : \A i \in Ids :
              [](i \in called[p] => <>(<<p, <<p, i>>>> \in got \/ ~alive[p] \/ i \notin called[p]))
================================================================================
