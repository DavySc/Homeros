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
\*   Pick    the picker opens again with a server's databases (PR 179)
\*           only after a :Connect on that server alone, only once, and
\*           not after a key reached sql_key since, nor while the
\*           terminal is open (it takes the keys first)
\*   Shown   a read the pick waits for that ends read, with no key since
\*           and the terminal shut, opens it
\* Variant "ok" is sql.c.  "loginbatch" sends the query only when the login
\* goes straight to idle and takes the next idle for the rows (PR 168
\* before 5c54708e); "noretry" reads at the first login only (PR 168 at
\* f88dda11, finding C); "keykeeps" does not clear the pick on a key,
\* "underterm" opens it under the terminal (PR 179's review, finding 4).
\* Residual: keys the terminal took and that it gave back before the read
\* ended are not seen (the terminal is shut by then).
EXTENDS Naturals
CONSTANTS Logins, Drops, Picks, Variant

VARIABLES sess,   \* the session: "out", "in"
          st,     \* the catalog: "none", "login", "reading", "read", "failed"
          conn,   \* its connection: "closed", "auth", "own" (the login's own batch), "busy" (the query)
          rows,   \* what the rows are: "none", "answer", "other"
          logins, \* session logins so far (bounds the model)
          drops,  \* fn NULLs so far
          bad,    \* "" or the property a step broke
          pick,   \* pickfor: "none", "wait" (a server alone picked), "open" (the picker opened again)
          keyed,  \* a key reached sql_key since the pick
          term,   \* the terminal is open
          picks   \* :Connects on the server alone so far
vars == <<sess, st, conn, rows, logins, drops, bad, pick, keyed, term, picks>>
pv == <<pick, keyed, term, picks>>

Init == sess = "out" /\ st = "none" /\ conn = "closed" /\ rows = "none" /\ logins = 0 /\ drops = 0 /\ bad = ""
        /\ pick = "none" /\ keyed = FALSE /\ term = FALSE /\ picks = 0

Read == st' = "login" /\ conn' = "auth" /\ rows' = "none"   \* catread: dial

\* the session logs in (wake): catread unless read or reading
Login == sess = "out" /\ logins < Logins /\ sess' = "in" /\ logins' = logins + 1
         /\ (IF st = "none" \/ (st = "failed" /\ Variant # "noretry") THEN Read ELSE UNCHANGED <<st, conn, rows>>)
         /\ bad' = (IF st = "failed" /\ st' # "login" THEN "Retry" ELSE bad)
         /\ UNCHANGED <<drops, pv>>

Logout == sess = "in" /\ sess' = "out" /\ UNCHANGED <<st, conn, rows, logins, drops, bad, pv>>

\* fn NULL: uncat, then read now if the session is in
Drop == drops < Drops /\ drops' = drops + 1
        /\ (IF sess = "in" THEN Read ELSE st' = "none" /\ conn' = "closed" /\ rows' = "none")
        /\ UNCHANGED <<sess, logins, bad, pv>>

\* uncat at a read's end: a waiting pick is spent; it opens the picker if
\* the read ended read, the terminal is shut (and no key cleared it)
Ended(read) == IF pick = "wait"
               THEN /\ pick' = (IF read /\ (~term \/ Variant = "underterm") THEN "open" ELSE "none")
                    /\ bad' = (IF pick' = "open" /\ (keyed \/ term) THEN "Pick"
                              ELSE IF pick' # "open" /\ read /\ ~keyed /\ ~term THEN "Shown" ELSE bad)
               ELSE UNCHANGED <<pick, bad>>

\* catwake on the connection going idle from state from
Idle(from) == IF st = "login" /\ (Variant # "loginbatch" \/ from = "auth")
              THEN st' = "reading" /\ conn' = "busy" /\ rows' = rows /\ UNCHANGED <<pick, bad>>   \* the query sent
              ELSE st' = "read" /\ conn' = "closed" /\ rows' = (IF from = "busy" THEN "answer" ELSE "other") /\ Ended(TRUE)

Auth == conn = "auth" /\ ((conn' = "own" /\ UNCHANGED <<st, rows, pick, bad>>) \/ Idle("auth")) /\ UNCHANGED <<sess, logins, drops, keyed, term, picks>>
Own == conn = "own" /\ Idle("own") /\ UNCHANGED <<sess, logins, drops, keyed, term, picks>>
Answer == conn = "busy" /\ Idle("busy") /\ UNCHANGED <<sess, logins, drops, keyed, term, picks>>
Fail == conn # "closed" /\ st' = "failed" /\ conn' = "closed" /\ rows' = "none" /\ Ended(FALSE) /\ UNCHANGED <<sess, logins, drops, keyed, term, picks>>

\* :Connect on the server alone (sql_connect): its catalog read again
\* (uncat, then catread), the pick waiting; a pick before closes the read
Pick == picks < Picks /\ picks' = picks + 1 /\ Read /\ pick' = "wait" /\ keyed' = FALSE
        /\ UNCHANGED <<sess, logins, drops, bad, term>>
\* a key while the terminal is shut reaches sql_key, which clears the pick
Key == ~term /\ keyed' = TRUE /\ pick' = (IF pick = "wait" /\ Variant # "keykeeps" THEN "none" ELSE pick)
       /\ UNCHANGED <<sess, st, conn, rows, logins, drops, bad, term, picks>>
\* Ctrl+. (tools_key): the terminal opens or shuts; its keys never reach sql_key
Term == term' = ~term /\ UNCHANGED <<sess, st, conn, rows, logins, drops, bad, pick, keyed, picks>>

Next == Login \/ Logout \/ Drop \/ Auth \/ Own \/ Answer \/ Fail \/ Pick \/ Key \/ Term
Spec == Init /\ [][Next]_vars /\ WF_vars(Auth) /\ WF_vars(Own) /\ WF_vars(Answer)

Safe == bad = "" /\ (st = "read" => rows = "answer")
Ends == conn # "closed" ~> conn = "closed"
====
