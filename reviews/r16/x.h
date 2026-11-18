/* One shape for both builds' answers, so one fuzzer can hold them side by side. */
#include <stddef.h>
typedef struct { size_t begin, post, cbeg, cend, end; int has, type; /* index in xnames */
	int level, todo, pri, commented; size_t ta, tz; unsigned taghash; } XN;
extern const char *const xnames[];
size_t xa_parse(const char *s, size_t n, XN **out);   /* build A; (size_t)-1 out of memory */
size_t xb_parse(const char *s, size_t n, XN **out);   /* build B */
