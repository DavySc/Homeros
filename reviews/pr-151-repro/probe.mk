include Makefile
probe: $(OBJS)
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o $(OUT) $(SRC) $(SRCS) $(OBJS) $(LIBS)
