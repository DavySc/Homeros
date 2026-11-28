/* a message over 511 bytes: is it cut inside a character? */
#include "org.h"
#include <stdio.h>
#include <string.h>
#include "uni.h"
int main(void)
{
	static char t[2000] = "see [[x";
	size_t n = 7;
	for (int i = 0; i < 300; i++) memcpy(t + n, "\xc3\xa9", 2), n += 2;
	memcpy(t + n, "]] x\n", 5); n += 5;
	OrgOpen o;
	org_open(t, n, 8, &o);
	size_t l = strlen(o.s);
	printf("kind %d, %zu bytes, last bytes %02x %02x: %s\n", o.kind, l, (unsigned char)o.s[l - 2], (unsigned char)o.s[l - 1], (o.s[l - 1] & 0xc0) == 0xc0 ? "CUT INSIDE A CHARACTER" : "ends at a character");
	return 0;
}
