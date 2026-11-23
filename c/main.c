/* zdb probe EXE SRC PROBES: runs EXE, stops at each probe line of SRC and prints each named
   variable as std.fmt "{any}" would, one per line (the oracle's protocol, oracle/gen.py). */
#include "value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { uint32_t line; char names[32][32]; int n; uint64_t addr[8]; uint32_t naddr; } Probe;

static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }

int session(const char *zig, const char *src);

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "session")) return session(argv[2], argv[3]);
    if (argc == 4 && !strcmp(argv[1], "lines")) {     /* zdb lines EXE FILE: the line table of FILE */
        Dwarf d;
        if (!dwarf_open(&d, argv[2])) return 2;
        printf("# %u units, %u dies, %u rows, %u files\n", d.nunits, d.ndies, d.nrows, d.nfiles);
        for (uint32_t i = 0; i < d.nrows; i++)
            if (strstr(d.files[d.rows[i].file].name, argv[3]))
                printf("%#llx %u%s%s\n", (unsigned long long)d.rows[i].addr, d.rows[i].line, d.rows[i].stmt ? " stmt" : "", d.rows[i].end ? " end" : "");
        return 0;
    }
    if (argc < 5 || strcmp(argv[1], "probe")) { fprintf(stderr, "usage: zdb probe EXE SRC PROBES | zdb session ZIG SRC\n"); return 2; }
    double t0 = now();
    Dwarf d;
    if (!dwarf_open(&d, argv[2])) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
    double t1 = now();
    Probe pr[64]; int np = 0;
    FILE *f = fopen(argv[4], "r");
    char line[4096];
    while (f && np < 64 && fgets(line, sizeof line, f)) {
        Probe *p = &pr[np];
        char *tok = strtok(line, " \n");
        if (!tok || strcmp(tok, "PROBE")) continue;
        p->line = (uint32_t)atoi(strtok(NULL, " \n"));
        p->n = 0;
        while ((tok = strtok(NULL, " \n")) && p->n < 32) snprintf(p->names[p->n++], 32, "%s", tok);
        p->naddr = line_addrs(&d, argv[3], p->line, p->addr, 8);
        if (!p->naddr) printf("# no code for line %u\n", p->line);
        np++;
    }
    if (f) fclose(f);
    uint64_t traps[512]; uint32_t nt = 0;
    for (int i = 0; i < np; i++) for (uint32_t k = 0; k < pr[i].naddr; k++) traps[nt++] = pr[i].addr[k];
    Target t;
    char *args[] = { argv[2], NULL };
    if (!target_start(&t, args)) { fprintf(stderr, "cannot start\n"); return 2; }
    uint64_t pc; int code;
    Out o = { 0 };
    while (target_run(&t, traps, nt, &pc, &code)) {
        Regs r;
        target_regs(&t, &r);
        for (int i = 0; i < np; i++)
            for (uint32_t k = 0; k < pr[i].naddr; k++) {
                if (pr[i].addr[k] != pc) continue;
                for (int j = 0; j < pr[i].n; j++) {
                    o.n = 0;
                    uint32_t v = var_at(&d, pc, pr[i].names[j]);
                    if (!v) out_add(&o, "<no variable %s>", pr[i].names[j]);
                    else print_var(&d, &t, &r, v, &o);
                    printf("%.*s\n", (int)o.n, o.s ? o.s : "");
                }
            }
    }
    printf("# zdb: exit %d, load %.1f ms, %u dies, %u rows, total %.1f ms\n", code, (t1 - t0) * 1e3, d.ndies, d.nrows, (now() - t0) * 1e3);
    return 0;
}
