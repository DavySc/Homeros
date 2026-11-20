/* review probe: git_real on a path typed in another case */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <limits.h>
int git_real(const char *path, char *out);
int main(void)
{
	char tmp[MAX_PATH], d[2 * MAX_PATH], q[2 * MAX_PATH], out[PATH_MAX];
	GetTempPathA(sizeof tmp, tmp);
	snprintf(d, sizeof d, "%sRvMixedCase", tmp);
	CreateDirectoryA(d, NULL);
	snprintf(q, sizeof q, "%srvmixedcase", tmp);
	printf("exists as typed: %d\n", GetFileAttributesA(q) != INVALID_FILE_ATTRIBUTES);
	printf("git_real: %d %s\n", git_real(q, out), out);
	return 0;
}
