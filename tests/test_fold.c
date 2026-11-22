/* Folds against Neovim 0.12.6: replays tests/test_fold.txt (or the trace
   named by argv[1]), which tests/test_fold.lua recorded from nvim --headless
   (300 random scenarios of 40 steps, seed 1; its comment gives the format),
   calling the fold module as Neovim's change.c, normal.c and edit.c call
   fold.c, and compares foldclosed(), foldclosedend() and foldlevel() of
   every line after every step. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "fold.h"

enum { MAXN = 512 };
static char line[MAXN][8];
static size_t n;

/* the oracle's FE() */
static int fe(void *ctx, size_t l, int *start)
{
	(void)ctx;
	size_t k = strspn(line[l], "*");
	if (k && !line[l][k]) { *start = 1; return (int)k; }
	if (line[l][0] >= '0' && line[l][0] <= '9' && !line[l][1]) return line[l][0] - '0';
	if (!strcmp(line[l], "y")) return -1;
	while (l--)
		if ((k = strspn(line[l], "*")) && !line[l][k]) return (int)k;
	return 0;
}

/* every line takes two screen rows: no fold is small, as with
   'foldminlines' 0 */
static int tall(void *ctx, size_t l)
{
	(void)ctx;
	(void)l;
	return 1;
}

static void put(size_t at, const char *tok)
{
	CHECK(n < MAXN && strlen(tok) < sizeof line[0]);
	memmove(line[at + 1], line[at], (n - at) * sizeof line[0]);
	strcpy(line[at], strcmp(tok, ".") ? tok : "");
	n++;
}

/* o, O and A<CR>: open_line() appends the line, which edit() opens */
static void open_line(Fold *f, char op, size_t c, const char *tok)
{
	size_t l1, l2, at;
	if (op == 'A') {   /* in Insert mode: no update */
		fold_cmd(f, FOLD_VIEW, c, 1);
		at = c + 1;
		fold_adjust(f, at, (size_t)-1, 1, 0, 1);
	} else {           /* n_opencmd(): below or above a closed fold */
		at = !fold_closed(f, c, &l1, &l2) ? c + (op == 'o') : op == 'o' ? l2 + 1 : l1;
		fold_adjust(f, at, (size_t)-1, 1, 0, 0);
	}
	put(at, ".");
	if (op != 'A') fold_update(f, at, at + 1, n);
	fold_cmd(f, FOLD_VIEW, at, 1);
	strcpy(line[at], strcmp(tok, ".") ? tok : "");
}

static int zcmd(Fold *f, const char *cmd, size_t c, long count)
{
	static const char *const name[] = { "za", "zc", "zo", "zO", "zR", "zM", "zv", "zX", "L" };
	int op = 0;
	while (strcmp(name[op], cmd)) CHECK(++op <= FOLD_LEVEL);
	if (op == FOLD_RESET) fold_update(f, 0, n, n);
	return fold_cmd(f, op, c, count);
}

int main(int argc, char **argv)
{
	FILE *in = fopen(argc > 1 ? argv[1] : "tests/test_fold.txt", "r");
	CHECK(in);
	static char buf[1 << 16], step[1024];
	Fold *f = NULL;
	int rc = 0, scen = 0, steps = 0;
	while (fgets(buf, sizeof buf, in)) {
		char *s = strtok(buf, " \n");
		if (!strcmp(s, "R")) {
			CHECK(atoi(strtok(NULL, " \n")) == rc);
			for (size_t l = 0; l < n; l++) {
				size_t l1 = 0, l2 = 0;
				int c = fold_closed(f, l, &l1, &l2);
				char got[64];
				int k = snprintf(got, sizeof got, "%d", fold_level(f, l));
				if (c) snprintf(got + k, sizeof got - (size_t)k, "/%zu/%zu", l1 + 1, l2 + 1);
				char *want = strtok(NULL, " \n");
				if (!want || strcmp(got, want)) {
					fprintf(stderr, "FAIL scenario %d, %s: line %zu: %s, nvim %s\n", scen, step, l + 1, got, want ? want : "none");
					fold_free(f);
					fclose(in);
					return 1;
				}
			}
			CHECK(!strtok(NULL, " \n"));
			steps++;
			continue;
		}
		snprintf(step, sizeof step, "%s %s", s, s + strlen(s) + 1);
		step[strcspn(step, "\n")] = 0;
		rc = 0;
		if (!strcmp(s, "S")) {
			fold_free(f);
			int lvl = atoi(strtok(NULL, " \n"));
			CHECK((f = fold_new(lvl, fe, atoi(strtok(NULL, " \n")) ? NULL : tall, NULL)));
			n = 0;
			for (char *t; (t = strtok(NULL, " \n"));) put(n, t);
			fold_update(f, n / 2, n / 2, n);   /* the first call does every line */
			scen++;
		} else if (!strcmp(s, "E")) {   /* nvim_buf_set_lines() */
			size_t a = strtoul(strtok(NULL, " \n"), NULL, 10), b = strtoul(strtok(NULL, " \n"), NULL, 10);
			memmove(line[a], line[b], (n - b) * sizeof line[0]);
			n -= b - a;
			size_t k = 0;
			for (char *t; (t = strtok(NULL, " \n"));) put(a + k++, t);
			long extra = (long)k - (long)(b - a);
			fold_adjust(f, a, b, b > a ? FOLD_DEL : 0, extra, 0);
			fold_update(f, a, (size_t)((long)b + extra), n);
		} else if (!strcmp(s, "Z")) {
			char *cmd = strtok(NULL, " \n");
			size_t c = strtoul(strtok(NULL, " \n"), NULL, 10);
			rc = zcmd(f, cmd, c, atol(strtok(NULL, " \n")));
		} else {
			size_t c = strtoul(strtok(NULL, " \n"), NULL, 10);
			open_line(f, *s, c, strtok(NULL, " \n"));
		}
	}
	fold_free(f);
	fclose(in);
	CHECK(scen > 0);
	printf("test_fold: %d scenarios, %d steps as nvim\n", scen, steps);
	return 0;
}
