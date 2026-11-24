/* net.c's Windows read pattern, alone: does a close that arrives with (or
   before) unread data still wake the event once the data is read?
   close.exe PORT SLEEP_MS : connects, sleeps so data and FIN are both there,
   then: wait event; WSAEnumNetworkEvents (resets); one recv of 64 KB. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
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
	static char b[65536];
	size_t got = 0, wakes = 0;
	for (;;) {
		if (WaitForSingleObject(ev, 3000) == WAIT_TIMEOUT) { printf("HANG: no wake for 3 s after %zu bytes, %zu wakes: the close was never seen\n", got, wakes); return 2; }
		if (!wakes++) Sleep((DWORD)atoi(argv[2]));
		WSANETWORKEVENTS ne;
		WSAEnumNetworkEvents(s, ev, &ne);
		printf("wake %zu events 0x%lx\n", wakes, ne.lNetworkEvents);
		int k = recv(s, b, sizeof b, 0);
		if (k < 0 && WSAGetLastError() == WSAEWOULDBLOCK) continue;
		if (k <= 0) { printf("close seen after %zu bytes, %zu wakes (recv %d, error %d)\n", got, wakes, k, k ? WSAGetLastError() : 0); return 0; }
		got += (size_t)k;
	}
}
