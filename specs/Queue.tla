------------------------------ MODULE Queue ------------------------------
(* The only shared state between the editor and the debugger's engine thread: two bounded
   queues (commands to the engine, events to the editor) and the editor's wake flag (an eventfd
   on Linux, a posted message on Windows).

   Properties, stated before the mechanism:
     Order       the editor receives exactly the events the engine produced, in order, once each
     Handled     every command the editor managed to send is handled
     Delivered   every event the engine produced reaches the editor
     Ends        after Quit the engine thread ends and the editor joins it
     JoinIsShort the editor only starts to join a thread that has nothing left to wait for
   The editor never waits: no editor action below has a guard on the engine's progress except
   Join, and JoinIsShort is the promise that makes that one safe.

   Mechanism: a push is "append under the lock, then wake the other side". The engine waits for
   a command only when the target is stopped (lock, test, wait: one atomic step); while the
   target runs it polls the queue between timed waits for debug events, as RAD does
   (win32_demon.c, WaitForDebugEvent with a timeout). The engine waits for room in the event
   queue; the editor, woken, clears the flag and then takes everything. The engine's last event
   is Exited; the editor joins only after it has received it.

   Variant picks the mechanism: "ok", or one of the broken ones that must fail. *)
EXTENDS Naturals, Sequences
CONSTANTS Cap, MaxCmds, Variant

VARIABLES cmdq, evq, wake,       \* the shared state
          eng, out, target,      \* engine: program counter, events still to push, target state
          ed, got,               \* editor: program counter, events received
          sent, handled, made,   \* history: commands sent, commands handled, events produced
          quit                   \* the editor has sent Quit
vars == <<cmdq, evq, wake, eng, out, target, ed, got, sent, handled, made, quit>>

Init == /\ cmdq = <<>> /\ evq = <<>> /\ wake = FALSE
        /\ eng = "take" /\ out = <<>> /\ target = "stopped"
        /\ ed = "loop" /\ got = <<>>
        /\ sent = 0 /\ handled = 0 /\ made = <<>> /\ quit = FALSE

\* ---- the editor ----
Send(c) == /\ ed = "loop" /\ ~quit /\ sent < MaxCmds /\ Len(cmdq) < Cap
           /\ cmdq' = Append(cmdq, c) /\ sent' = sent + 1 /\ quit' = (c = "quit")
           /\ eng' = IF eng = "asleep" THEN "take" ELSE eng       \* the wake that "lostwake" can miss
           /\ ed' = IF c = "quit" /\ Variant = "joinearly" THEN "join" ELSE ed
           /\ UNCHANGED <<evq, wake, out, target, got, handled, made>>

Take == /\ got' = got \o evq /\ evq' = <<>>
SawExit(s) == \E i \in 1..Len(s) : s[i] = "exited"

\* ok: clear the flag, then take. drainfirst: take, then clear.
Woken == /\ ed = "loop" /\ wake
         /\ IF Variant = "drainfirst" THEN Take /\ ed' = "clear" /\ UNCHANGED wake
                                      ELSE wake' = FALSE /\ ed' = "take" /\ UNCHANGED <<got, evq>>
         /\ UNCHANGED <<cmdq, eng, out, target, sent, handled, made, quit>>
Second == /\ \/ ed = "take" /\ Take /\ ed' = (IF SawExit(got') THEN "join" ELSE "loop") /\ UNCHANGED wake
             \/ ed = "clear" /\ wake' = FALSE /\ ed' = (IF SawExit(got) THEN "join" ELSE "loop") /\ UNCHANGED <<got, evq>>
          /\ UNCHANGED <<cmdq, eng, out, target, sent, handled, made, quit>>
Join == /\ ed = "join" /\ eng = "done" /\ ed' = "joined"
        /\ UNCHANGED <<cmdq, evq, wake, eng, out, target, got, sent, handled, made, quit>>
Editor == Woken \/ Second \/ Join

\* ---- the engine ----
Events(c) == CASE c = "work" -> <<"reply">>
               [] c = "run"  -> IF target = "stopped" THEN <<"running">> ELSE <<>>
               [] c = "halt" -> IF target = "running" THEN <<"stopped">> ELSE <<>>
               [] c = "quit" -> <<"exited">>
After(c) == CASE c = "run" -> "running" [] c = "halt" -> "stopped" [] c = "quit" -> "none" [] OTHER -> target
First(o) == IF o = <<>> THEN "take" ELSE IF Variant = "signalfirst" THEN "signal" ELSE "push"

\* Stopped target: wait for a command. Running target: this is the poll between timed waits.
Pop == /\ eng = "take" /\ cmdq # <<>>
       /\ LET c == Head(cmdq) IN
            /\ cmdq' = Tail(cmdq) /\ handled' = handled + 1
            /\ out' = Events(c) /\ target' = After(c) /\ eng' = First(Events(c))
       /\ UNCHANGED <<evq, wake, ed, got, sent, made, quit>>
\* lostwake: test and sleep are two steps, and a wake between them finds nobody asleep
Doze == /\ Variant = "lostwake" /\ target = "stopped"
        /\ \/ eng = "take" /\ cmdq = <<>> /\ eng' = "dozing"
           \/ eng = "dozing" /\ eng' = "asleep"
        /\ UNCHANGED <<cmdq, evq, wake, out, target, ed, got, sent, handled, made, quit>>
Finished == IF out = <<>> /\ target = "none" THEN "done" ELSE First(out)
Push == /\ eng = "push" /\ Len(evq) < Cap
        /\ evq' = Append(evq, Head(out)) /\ made' = Append(made, Head(out)) /\ out' = Tail(out)
        /\ eng' = IF Variant = "signalfirst" THEN (IF out' = <<>> /\ target = "none" THEN "done" ELSE First(out')) ELSE "signal"
        /\ UNCHANGED <<cmdq, wake, target, ed, got, sent, handled, quit>>
Signal == /\ eng = "signal" /\ wake' = TRUE
          /\ eng' = IF Variant = "signalfirst" THEN "push" ELSE Finished
          /\ UNCHANGED <<cmdq, evq, out, target, ed, got, sent, handled, made, quit>>
Engine == Pop \/ Doze \/ Push \/ Signal

\* the target stops by itself (a breakpoint, an exception, its exit)
Stops == /\ eng = "take" /\ target = "running"
         /\ target' = "stopped" /\ out' = <<"stopped">> /\ eng' = First(out')
         /\ UNCHANGED <<cmdq, evq, wake, ed, got, sent, handled, made, quit>>

Next == (\E c \in {"work", "run", "halt", "quit"} : Send(c)) \/ Editor \/ Engine \/ Stops
Spec == Init /\ [][Next]_vars /\ WF_vars(Editor) /\ WF_vars(Engine)

\* ---- properties ----
Bounded     == Len(cmdq) <= Cap /\ Len(evq) <= Cap
Order       == /\ Len(got) + Len(evq) = Len(made)
               /\ got \o evq = made
               /\ \A i \in 1..Len(made) : made[i] = "exited" => i = Len(made)
JoinIsShort == ed \in {"join", "joined"} => eng \in {"signal", "done"} \/ Variant = "joinearly"
Handled     == <>[](handled = sent)
Delivered   == <>[](got = made)
Ends        == quit ~> (ed = "joined")
=============================================================================
