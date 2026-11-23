# make -f Makefile -f reviews/pr-121-repro/run.mk probe P=stale
probe: $(OBJS) | build/san
	gcc $(WARN) $(CPPFLAGS) $(SAN) -Itests -o build/san/probe_$(P) reviews/pr-121-repro/$(P).c $(SRCS) $(OBJS) $(LIBS)
	./build/san/probe_$(P)
# make -f Makefile -f reviews/pr-121-repro/run.mk t T=syntax   (one test, without make test's tail)
t: $(OBJS) | build/san
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/test_$(T) tests/test_$(T).c $(SRCS) $(OBJS) $(LIBS)
	./build/san/test_$(T)
# -O2, no sanitizers: make -f Makefile -f reviews/pr-121-repro/run.mk perf OUT=build/perf
perf: $(OBJS)
	gcc $(WARN) $(CPPFLAGS) -O2 -o $(OUT) reviews/pr-121-repro/perf.c $(SRCS) $(OBJS) $(LIBS)
