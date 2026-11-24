/* remote.h on Windows (Remote.tla "ok"): with no editor running a launch
   becomes it; a later launch hands over its file made absolute ("" for none)
<<<<<<< HEAD
   and is told it was taken; eight launches started together each hand
   theirs over once, and with no editor one of them becomes it and takes
   the other seven; a launch whose turn's owner died inside still gets
   through; another program's WM_COPYDATA, or one without its NUL, is
   refused; once the editor stops, a launch runs its file itself.
=======
   and is told it was taken; while the turn is another's, a launch waits
   for it and hands nothing over; eight launches started together each hand
   theirs over once, and with no editor one of them becomes it and takes
   the other seven; a launch whose turn's owner died inside still gets
   through; a launch whose file has no full name runs it itself and is no
   second editor; another program's WM_COPYDATA, an empty one or one
   without its NUL is refused; files are taken in the order they came;
   once the editor stops, a launch runs its file itself.
>>>>>>> pr156b
   This program is the editor; each launch is this program again, run with
   "launch FILE" ("-": none), which exits 1 when the editor took the file;
   with "die" it takes the turn and exits holding it.
   Not while a homeros of this build runs in the session: it would be the
   editor, and the first CHECK says so.
   Runs on Windows or under Wine (make win); elsewhere it says so. */
#ifdef _WIN32
#include <windows.h>
#include <limits.h>
#include <string.h>
#include "check.h"
#include "file.h"
#include "remote.h"

enum { N = 8 };

static HANDLE launch(const char *how, const char *file)
{
<<<<<<< HEAD
	char self[MAX_PATH], line[2 * MAX_PATH];
=======
	char self[MAX_PATH], line[2 * PATH_MAX];
>>>>>>> pr156b
	STARTUPINFOA si = { .cb = sizeof si };
	PROCESS_INFORMATION pi;
	CHECK(GetModuleFileNameA(NULL, self, sizeof self) < sizeof self);
	snprintf(line, sizeof line, "\"%s\" %s %s", self, how, file);
	CHECK(CreateProcessA(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi));
	CloseHandle(pi.hThread);
	return pi.hProcess;
}

/* As the editor's loop: read messages until launch p exits (10 s at most);
   its exit code */
static DWORD finish(HANDLE p)
{
	MSG m;
	DWORD code = STILL_ACTIVE;
	while (MsgWaitForMultipleObjects(1, &p, FALSE, 10000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
		while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
	CHECK(GetExitCodeProcess(p, &code) && code != STILL_ACTIVE);
	CloseHandle(p);
	return code;
}

<<<<<<< HEAD
=======
/* As the editor's loop for ms milliseconds, or until launch p exits */
static void pump(HANDLE p, DWORD ms)
{
	MSG m;
	for (DWORD t0 = GetTickCount(), t; (t = GetTickCount() - t0) < ms
	     && MsgWaitForMultipleObjects(1, &p, FALSE, ms - t, QS_ALLINPUT) == WAIT_OBJECT_0 + 1; )
		while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
}

>>>>>>> pr156b
static int took(const char *file)
{
	char want[PATH_MAX] = "", *got = remote_next();
	if (file) CHECK(!file_full(file, want));
	int r = got && !strcmp(got, want);
	free(got);
	return r;
}

static LRESULT copy(ULONG_PTR tag, const char *s, DWORD n)
{
	COPYDATASTRUCT cd = { tag, n, (void *)s };
	HWND h = FindWindowExW(HWND_MESSAGE, NULL, L"homeros-remote", NULL);
	CHECK(h);
	return SendMessageW(h, WM_COPYDATA, 0, (LPARAM)&cd);
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "launch")) return remote_open(strcmp(argv[2], "-") ? argv[2] : NULL);
<<<<<<< HEAD
=======
	/* as "launch", once it said it is about to take its turn */
	if (argc == 3 && !strcmp(argv[1], "wait")) {
		SetEvent(OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\homeros-remote-wait"));
		return remote_open(argv[2]);
	}
	/* as "launch", then says so and stays until told to exit */
	if (argc == 3 && !strcmp(argv[1], "keep")) {
		int r = remote_open(argv[2]);
		SignalObjectAndWait(OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\homeros-remote-kept"),
		                    OpenEventW(SYNCHRONIZE, FALSE, L"Local\\homeros-remote-release"), 10000, FALSE);
		return r;
	}
>>>>>>> pr156b
	/* as "launch", started when all are ready and the test says go, so the
	   launches meet in remote_open; one that becomes the editor reads
	   messages until it took the other N - 1 files (10 s at most) and exits
	   with how many it took (a launch that handed its own over: 100) */
	if (argc == 3 && !strcmp(argv[1], "serve")) {
		ReleaseSemaphore(OpenSemaphoreW(SEMAPHORE_MODIFY_STATE, FALSE, L"Local\\homeros-remote-ready"), 1, NULL);
		WaitForSingleObject(OpenEventW(SYNCHRONIZE, FALSE, L"Local\\homeros-remote-go"), 10000);
		if (remote_open(argv[2])) return 100;
		MSG m;
		int n = 0;
		for (DWORD t0 = GetTickCount(); n < N - 1 && GetTickCount() - t0 < 10000; ) {
			MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT);
			while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
			for (char *s; (s = remote_next()); free(s)) n++;
		}
		return n;
	}
	/* takes the turn, says so, and exits holding it when told */
	if (argc == 2 && !strcmp(argv[1], "die")) {
		HANDLE m = CreateMutexW(NULL, TRUE, L"Local\\homeros-remote");
		if (GetLastError() == ERROR_ALREADY_EXISTS) WaitForSingleObject(m, INFINITE);
		SignalObjectAndWait(OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\homeros-remote-held"),
		                    OpenEventW(SYNCHRONIZE, FALSE, L"Local\\homeros-remote-die"), 10000, FALSE);
		ExitProcess(3);
	}

	CHECK(!remote_open("a.txt") && !remote_next());
	CHECK(finish(launch("launch", "b.txt")) == 1 && took("b.txt") && !remote_next());
	CHECK(finish(launch("launch", "-")) == 1 && took(NULL));

<<<<<<< HEAD
=======
	/* a launch whose file has no full name (too long) runs it itself, and
	   does not become a second editor beside this one */
	char longname[PATH_MAX + 8];
	memset(longname, 'n', PATH_MAX);
	strcpy(longname + PATH_MAX, ".txt");
	HANDLE kept = CreateEventW(NULL, TRUE, FALSE, L"Local\\homeros-remote-kept");
	HANDLE release = CreateEventW(NULL, FALSE, FALSE, L"Local\\homeros-remote-release");
	HANDLE k = launch("keep", longname);
	pump(kept, 10000);   /* a launch that sent its file waits until it is read */
	CHECK(WaitForSingleObject(kept, 0) == WAIT_OBJECT_0 && !remote_next());
	HWND first = FindWindowExW(HWND_MESSAGE, NULL, L"homeros-remote", NULL);
	DWORD pid = 0;
	CHECK(first && GetWindowThreadProcessId(first, &pid) && pid == GetCurrentProcessId()
	      && !FindWindowExW(HWND_MESSAGE, first, L"homeros-remote", NULL));
	CHECK(SetEvent(release) && finish(k) == 0 && !remote_next());
	CloseHandle(kept);
	CloseHandle(release);

	/* while another holds the turn, a launch waits for it: it hands nothing
	   over though the editor reads messages, and goes on once the turn is
	   free (no race: a launch that skipped the turn hands g.txt over here) */
	HANDLE turn = CreateMutexW(NULL, TRUE, L"Local\\homeros-remote");
	CHECK(turn && (GetLastError() != ERROR_ALREADY_EXISTS || WaitForSingleObject(turn, 10000) == WAIT_OBJECT_0));
	HANDLE waiting = CreateEventW(NULL, TRUE, FALSE, L"Local\\homeros-remote-wait");
	HANDLE w = launch("wait", "g.txt");
	CHECK(WaitForSingleObject(waiting, 10000) == WAIT_OBJECT_0);
	pump(w, 1000);
	CHECK(WaitForSingleObject(w, 0) == WAIT_TIMEOUT && !remote_next());
	CHECK(ReleaseMutex(turn));
	CHECK(finish(w) == 1 && took("g.txt"));
	CloseHandle(turn);
	CloseHandle(waiting);

>>>>>>> pr156b
	/* together, as Explorer opens several selected files */
	HANDLE p[N];
	char name[N][16];
	int seen = 0;
	for (int i = 0; i < N; i++) snprintf(name[i], sizeof name[i], "f%d.txt", i), p[i] = launch("launch", name[i]);
	for (int i = 0; i < N; i++) CHECK(finish(p[i]) == 1);
	for (char *s; (s = remote_next()); free(s))
		for (int i = 0; i < N; i++) {
			size_t n = strlen(s), k = strlen(name[i]);
			if (n > k && s[n - k - 1] == '\\' && !strcmp(s + n - k, name[i])) { CHECK(!(seen & 1 << i)); seen |= 1 << i; }
		}
	CHECK(seen == (1 << N) - 1);

	/* the turn outlives its dead owner while we hold the mutex open, so the
	   next launch finds it abandoned however late it comes */
	HANDLE held = CreateEventW(NULL, FALSE, FALSE, L"Local\\homeros-remote-held");
	HANDLE die = CreateEventW(NULL, FALSE, FALSE, L"Local\\homeros-remote-die");
	HANDLE d = launch("die", "");
	CHECK(WaitForSingleObject(held, 10000) == WAIT_OBJECT_0);
	HANDLE keep = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\homeros-remote");
	CHECK(keep && SetEvent(die) && finish(d) == 3);
	CHECK(finish(launch("launch", "c.txt")) == 1 && took("c.txt"));
	CloseHandle(keep);

<<<<<<< HEAD
	CHECK(!copy(1, "x.txt", 6) && !copy(0x686f6d72, "x.txt", 5) && !remote_next());
	char *s = NULL;
	CHECK(copy(0x686f6d72, "x.txt", 6) == 1 && (s = remote_next()) && !strcmp(s, "x.txt"));
	free(s);
=======
	CHECK(!copy(1, "x.txt", 6) && !copy(0x686f6d72, "x.txt", 5) && !copy(0x686f6d72, NULL, 0) && !remote_next());
	/* taken in the order they came */
	char *s = NULL, *s2 = NULL;
	CHECK(copy(0x686f6d72, "x.txt", 6) == 1 && copy(0x686f6d72, "y.txt", 6) == 1);
	CHECK((s = remote_next()) && !strcmp(s, "x.txt") && (s2 = remote_next()) && !strcmp(s2, "y.txt") && !remote_next());
	free(s);
	free(s2);
>>>>>>> pr156b

	remote_stop();
	CHECK(!FindWindowExW(HWND_MESSAGE, NULL, L"homeros-remote", NULL));
	CHECK(finish(launch("launch", "d.txt")) == 0 && !remote_next());
	/* eight together with no editor: one becomes it, the other seven hand
	   it theirs */
	int eds = 0;
	HANDLE go = CreateEventW(NULL, TRUE, FALSE, L"Local\\homeros-remote-go");
	HANDLE ready = CreateSemaphoreW(NULL, 0, N, L"Local\\homeros-remote-ready");
	for (int i = 0; i < N; i++) p[i] = launch("serve", name[i]);
	for (int i = 0; i < N; i++) CHECK(WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0);
	CHECK(SetEvent(go));
	for (int i = 0; i < N; i++) {
		DWORD c = finish(p[i]);
		CHECK(c == 100 || c == N - 1);
		eds += c != 100;
	}
	CHECK(eds == 1);
	/* and the next one becomes the editor again */
	CHECK(!remote_open(NULL));
	CHECK(finish(launch("launch", "e.txt")) == 1 && took("e.txt"));
	remote_stop();
<<<<<<< HEAD
	puts("test_win_remote: the first launch is the editor; a later one hands over its file, absolute, or none; "
	     "eight together each once, and with no editor one becomes it; through a turn whose owner died; foreign or unterminated data refused; none taken once stopped");
=======
	puts("test_win_remote: the first launch is the editor; a later one hands over its file, absolute, or none; one whose file has no full name runs it, no second editor; "
	     "one waits for a turn held by another; eight together each once, and with no editor one becomes it; through a turn whose owner died; "
	     "foreign, empty or unterminated data refused; files taken in order; none taken once stopped");
>>>>>>> pr156b
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_win_remote: Windows only (make win runs it under Wine)"); return 0; }
#endif
