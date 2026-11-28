include Makefile
many: $(OBJS)
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/many reviews/pr-177-repro/many.c $(SRCS) $(OBJS) $(LIBS)
