# build a planted driver per NOT CAUGHT plant and run the directed texts
cd /home/claude/rev3; export PATH=$PWD/tools-env/bin:$PATH
cp src/org/org.c /tmp/claude-0/r3orig.c
grep -E "$1" /tmp/claude-0/plants/review.txt | while IFS='~' read -r name target prep file old new; do
	cp /tmp/claude-0/r3orig.c src/org/org.c
	python3 /tmp/claude-0/plants/apply.py src/org/org.c "$old" "$new" || continue
	make --eval 'drv: $(OBJS) ; gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/org_drv_t tests/org_drv.c $(LIB_SRCS) $(OBJS) $(LIBS)' drv 2>&1 | grep " error"
	echo "=== $name"
	for m in -f -s; do python3 tools/org_conform.py $m build/san/org_drv_t /tmp/claude-0/dir/*.org 2>&1 | grep -E "^[a-z0-9]+\.org|differ"; done
done
cp /tmp/claude-0/r3orig.c src/org/org.c
