# make -f Makefile -f reviews/pr-90-repro/probe.mk probe P=<dir of probe.c>
probe: $(OBJS) | build
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/asking $(P)/asking.c $(SRCS) $(OBJS) $(LIBS)
	gcc $(WARN) $(CPPFLAGS) -O2 -o build/probe $(P)/probe.c $(SRCS) $(OBJS) $(LIBS)
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/probe-san $(P)/probe.c $(SRCS) $(OBJS) $(LIBS)
