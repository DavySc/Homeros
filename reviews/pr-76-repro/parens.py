import random, sys
r = random.Random(int(sys.argv[1]))
W = ["(", "(", ")", ")", "\n", "\n", "\n", "select", "where", "and", "or", "on", "join", "left join", "GO\n", ";", "begin", "end", "x", "1", "=", ",", "--c\n", "/*c*/", "from t", "having", "with L as", "exists", "in", "insert into t", "values", "case", "end"]
print(" ".join(r.choice(W) for _ in range(r.randint(3, 60))))
