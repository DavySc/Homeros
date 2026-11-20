/* Finding 1: \w after Lean has answered initialize.  late 0: the bridge
   is opened before the loop turns (the unit test's order); late 1: the
   file is open and Lean has answered first (the order of use). */
#include "common.h"
int main(int argc, char **argv)
{
	int rc;
	if (helper(argc, argv, &rc)) return rc;
	int late = argc > 1 && atoi(argv[1]);
	setup();
	CHECK(!core_open(&e, path));
	if (late) turn(1000);              /* Lean answers initialize, publishes diagnostics */
	CHECK(open_bridge());
	Stream A = { -1, { 0 }, { 0 } };
	Stream *one[] = { &A };
	stream_open(&A, "pageA");
	bridge_update(&e);
	int hello = wait_for(one, 1, &A, "\"serverRestarted\"");
	/* a later move of the cursor does not help either */
	e.win->line = 1; bridge_update(&e);
	pump(one, 1, 500);
	printf("late=%d hello=%d initialize=%d events=%d\n", late, hello, has(&A, "\"m\":\"initialize\""), count(A.ev.b, "\n"));
	bridge_free();
	turn(200);
	core_free(&e);
	cleanup();
	return 0;
}
