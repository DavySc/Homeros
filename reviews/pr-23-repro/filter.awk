# Drops the cases oil itself failed on ("S" lines) from oil2.lua's output.
/^C / || /^\.$/ { if (n && !skip) printf "%s", buf; else if (n) dropped++; buf = ""; n = 0; skip = 0 }
/^S [0-9]+ vim\/_core/ { skip = 1; print > "/dev/stderr" }
{ buf = buf $0 "\n"; n++ }
END { if (n && !skip) printf "%s", buf; print "dropped " dropped+0 > "/dev/stderr" }
