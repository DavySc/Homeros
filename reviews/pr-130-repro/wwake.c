/* net.c's order on a wake (tds.c io(): net_flush, then net_read, whose
   WSAEnumNetworkEvents resets the event), alone: when the socket turns
   writable between the send that would block and the reset, is the write
   wake lost?   wwake.exe PORT PAUSE_MS
   The server (wsrv.py) reads nothing for 1 s, then reads everything.
   PAUSE_MS stands for the time between the two calls. */
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
	WSADATA d; WSAStartup(MAKEWORD(2, 2), &d);
	if (argc < 3) return 9;
	SOCKET s = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	WSAEVENT ev = WSACreateEvent();
	WSAEventSelect(s, ev, FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE);
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((u_short)atoi(argv[1])) };
	a.sin_addr.s_addr = htonl(0x7F000001);
	connect(s, (struct sockaddr *)&a, sizeof a);
	WSANETWORKEVENTS ne;
	WaitForSingleObject(ev, 3000), WSAEnumNetworkEvents(s, ev, &ne);   /* connected */
	size_t n = 16 << 20, at = 0;
	char *b = malloc(n);
	memset(b, 'x', n);
	/* net_flush's send, message after message, until one would block */
	int k = 0;
	while (at < ((size_t)1 << 30) && (k = send(s, b, (int)n, 0)) > 0) at += (size_t)k;
	printf("%zu bytes taken before a send said %d, error %d\n", at, k, k < 0 ? WSAGetLastError() : 0);
	if (k >= 0 || WSAGetLastError() != WSAEWOULDBLOCK) return puts("no send would block here: nothing to show"), 3;
	Sleep((DWORD)atoi(argv[2]));               /* the server drains meanwhile */
	WSAEnumNetworkEvents(s, ev, &ne);          /* net_read's woke() */
	printf("reset took events 0x%lx\n", ne.lNetworkEvents);
	char r[16];
	recv(s, r, sizeof r, 0);
	if (WaitForSingleObject(ev, 5000) == WAIT_TIMEOUT) return puts("STALL: bytes still queued, the socket is writable, and no wake for 5 s"), 2;
	WSAEnumNetworkEvents(s, ev, &ne);
	printf("woken: events 0x%lx\n", ne.lNetworkEvents);
	return 0;
}
