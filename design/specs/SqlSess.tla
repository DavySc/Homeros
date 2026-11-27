---- MODULE SqlSess ----
\* A SQL buffer's run in the editor (src/sql/sql.c): F5 starts a run of B
\* batches on the buffer's connection, Alt+Break cancels it, the user edits
\* the buffer at any time, the server answers each batch and each cancel.
\* The properties come first (the review of PR 153):
\*   Snapshot     every batch sent is the text as it was at its run's F5
\*   ReadySend    a batch is sent only while the server is idle
\*   CancelStops  no batch of a cancelled run is sent after its Break
\*   Ends         a run always ends
\* Variant "ok" is sql.c: the run copies its text at F5 and stays busy
\* until the server has answered the cancel.  "live" reads each batch from
\* the buffer when it is sent; "earlyidle" ends the run at Break, before
\* the server's answer (PR 153 at 4edc86f0: findings 1 and 2); "keep" goes
\* on with the batches after the cancel is answered.
EXTENDS Naturals
CONSTANTS B, Edits, Runs, Variant

VARIABLES ver,    \* the buffer's version (each edit one more)
          st,     \* the run: "idle", "running", "cancelling"
          snap,   \* the version at this run's F5
          left,   \* its batches not sent yet
          srv,    \* the server: "ready", "busy" (a batch), "cancel" (a cancel to answer)
          brk,    \* Break came in this run
          runs,   \* F5s so far (bounds the model)
          bad     \* "" or the property a step broke
vars == <<ver, st, snap, left, srv, brk, runs, bad>>

Init == ver = 0 /\ st = "idle" /\ snap = 0 /\ left = 0 /\ srv = "ready" /\ brk = FALSE /\ runs = 0 /\ bad = ""

Edit == ver < Edits /\ ver' = ver + 1 /\ UNCHANGED <<st, snap, left, srv, brk, runs, bad>>

F5 == st = "idle" /\ runs < Runs
      /\ st' = "running" /\ snap' = ver /\ left' = B /\ brk' = FALSE /\ runs' = runs + 1
      /\ UNCHANGED <<ver, srv, bad>>

\* next(): the run's next batch, once the previous one is answered
Send == st = "running" /\ left > 0 /\ srv # "busy"
        /\ bad' = (IF (IF Variant = "live" THEN ver ELSE snap) # snap THEN "Snapshot"
                   ELSE IF srv # "ready" THEN "ReadySend"
                   ELSE IF brk THEN "CancelStops" ELSE bad)
        /\ srv' = "busy" /\ left' = left - 1
        /\ UNCHANGED <<ver, st, snap, brk, runs>>

\* the server's answer to a batch: the run ends after its last
Done == srv = "busy" /\ srv' = "ready"
        /\ st' = (IF st = "running" /\ left = 0 THEN "idle" ELSE st)
        /\ UNCHANGED <<ver, snap, left, brk, runs, bad>>

Break == st = "running" /\ brk' = TRUE /\ left' = (IF Variant = "keep" THEN left ELSE 0)
         /\ (IF srv = "busy"
             THEN srv' = "cancel" /\ st' = (IF Variant = "earlyidle" THEN "idle" ELSE "cancelling")
             ELSE srv' = srv /\ st' = "idle")   \* between batches or logging in: nothing runs on the server
         /\ UNCHANGED <<ver, snap, runs, bad>>

\* the server's answer to the cancel (DONE_ATTN; Postgres: ReadyForQuery and the closed cancel connection)
Ack == srv = "cancel" /\ srv' = "ready"
       /\ st' = (IF st = "cancelling" THEN (IF Variant = "keep" /\ left > 0 THEN "running" ELSE "idle") ELSE st)
       /\ UNCHANGED <<ver, snap, left, brk, runs, bad>>

Next == Edit \/ F5 \/ Send \/ Done \/ Break \/ Ack
Spec == Init /\ [][Next]_vars /\ WF_vars(Send) /\ WF_vars(Done) /\ WF_vars(Ack)

Safe == bad = ""
Ends == st # "idle" ~> st = "idle"
====
