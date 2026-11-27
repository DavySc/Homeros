---- MODULE SqlCat ----
\* A connection's catalog in the editor (src/sql/sql.c, sql_catalog): a
\* session on the connection logs in (F5) and out (:Disconnect, :Connect);
\* the catalog's own connection logs in, may run a batch of its own first
\* (TDS's), then answers the catalog's query; any of its steps may fail;
\* completion drops the rows at any time (fn NULL).
\* The properties come first (the review of PR 168):
\*   Rows    the rows kept are the query's answer, never the login's batch
\*   Retry   a session's login after a failed read starts a new read
\*   Ends    a read that started ends (read or failed)
\* Variant "ok" is sql.c.  "loginbatch" sends the query only when the login
\* goes straight to idle and takes the next idle for the rows (PR 168
\* before 5c54708e); "noretry" reads at the first login only (PR 168 at
\* f88dda11, finding C).
EXTENDS Naturals
CONSTANTS Logins, Drops, Variant

VARIABLES sess,   \* the session: "out", "in"
          st,     \* the catalog: "none", "login", "reading", "read", "failed"
          conn,   \* its connection: "closed", "auth", "own" (the login's own batch), "busy" (the query)
          rows,   \* what the rows are: "none", "answer", "other"
          logins, \* session logins so far (bounds the model)
          drops,  \* fn NULLs so far
          bad     \* "" or the property a step broke
vars == <<sess, st, conn, rows, logins, drops, bad>>

Init == sess = "out" /\ st = "none" /\ conn = "closed" /\ rows = "none" /\ logins = 0 /\ drops = 0 /\ bad = ""

Read == st' = "login" /\ conn' = "auth" /\ rows' = "none"   \* catread: dial

\* the session logs in (wake): catread unless read or reading
Login == sess = "out" /\ logins < Logins /\ sess' = "in" /\ logins' = logins + 1
         /\ (IF st = "none" \/ (st = "failed" /\ Variant # "noretry") THEN Read ELSE UNCHANGED <<st, conn, rows>>)
         /\ bad' = (IF st = "failed" /\ st' # "login" THEN "Retry" ELSE bad)
         /\ UNCHANGED drops

Logout == sess = "in" /\ sess' = "out" /\ UNCHANGED <<st, conn, rows, logins, drops, bad>>

\* fn NULL: uncat, then read now if the session is in
Drop == drops < Drops /\ drops' = drops + 1
        /\ (IF sess = "in" THEN Read ELSE st' = "none" /\ conn' = "closed" /\ rows' = "none")
        /\ UNCHANGED <<sess, logins, bad>>

\* catwake on the connection going idle from state from
Idle(from) == IF st = "login" /\ (Variant # "loginbatch" \/ from = "auth")
              THEN st' = "reading" /\ conn' = "busy" /\ rows' = rows   \* the query sent
              ELSE st' = "read" /\ conn' = "closed" /\ rows' = (IF from = "busy" THEN "answer" ELSE "other")

Auth == conn = "auth" /\ ((conn' = "own" /\ UNCHANGED <<st, rows>>) \/ Idle("auth")) /\ UNCHANGED <<sess, logins, drops, bad>>
Own == conn = "own" /\ Idle("own") /\ UNCHANGED <<sess, logins, drops, bad>>
Answer == conn = "busy" /\ Idle("busy") /\ UNCHANGED <<sess, logins, drops, bad>>
Fail == conn # "closed" /\ st' = "failed" /\ conn' = "closed" /\ rows' = "none" /\ UNCHANGED <<sess, logins, drops, bad>>

Next == Login \/ Logout \/ Drop \/ Auth \/ Own \/ Answer \/ Fail
Spec == Init /\ [][Next]_vars /\ WF_vars(Auth) /\ WF_vars(Own) /\ WF_vars(Answer)

Safe == bad = "" /\ (st = "read" => rows = "answer")
Ends == conn # "closed" ~> conn = "closed"
====
