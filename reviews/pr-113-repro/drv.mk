include Makefile
DRV ?= /tmp/claude-0/drv/drive
drive-oracle: $(OBJS)
	$(CC) $(WARN) $(CPPFLAGS) -O1 -g -o $(DRV) $(SRCS) $(OBJS) src/infoview/oracle/drive.c $(LIBS)
drive-san: $(OBJS)
	$(CC) $(WARN) $(CPPFLAGS) $(SAN) -o $(DRV) $(SRCS) $(OBJS) src/infoview/oracle/drive.c $(LIBS)
