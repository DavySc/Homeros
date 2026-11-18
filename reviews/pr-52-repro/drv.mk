# make -f Makefile -f reviews/pr-52-repro/drv.mk DRV=path/to/x.c build/san/x
# builds a driver as `make test` builds tests/conform_drv.c
build/san/%: $(DRV) $(OBJS) | build/san
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o $@ $(DRV) $(LIB_SRCS) $(OBJS) $(LIBS)
