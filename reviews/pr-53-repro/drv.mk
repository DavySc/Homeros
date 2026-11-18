# Review of PR 53: builds a driver against the whole editor, as the Makefile
# builds conform_drv.  make -f Makefile -f reviews/pr-53-repro/drv.mk drv DRV=file.c OUT=name
drv: $(OBJS) | build/san
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/$(OUT) $(DRV) $(LIB_SRCS) $(OBJS) $(LIBS)
