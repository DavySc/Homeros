/* zdb session ZIG SRC: a debugging session driven by commands on stdin, one per line:
     b LINE   break at LINE of SRC        c   continue        p NAME   print a variable
     bt       backtrace (frame pointers)  w   the file was saved: rebuild and patch the process
     d        delete every breakpoint     q   quit
   Answers go to stdout, the program's own output to its stderr. */
#include "ec.h"
#include "value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }

static bool copy_file(const char *from, const char *to) {
    FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
    char buf[1 << 16]; size_t n; bool ok = a && b;
    while (ok && (n = fread(buf, 1, sizeof buf, a)) > 0) ok = fwrite(buf, 1, n, b) == n;
    if (a) fclose(a);
    if (b) fclose(b);
    return ok;
}

static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    s[fread(s, 1, (size_t)n, f)] = 0;
    fclose(f);
    return s;
}

/* pc, then each return address up the frame pointer chain; fps[i]: frame i's rbp */
static int frames(const Target *t, uint64_t *out, uint64_t *fps, int room) {
    Regs r;
    target_regs(t, &r);
    int n = 0;
    uint64_t fp = r.r[6];
    fps[n] = fp; out[n++] = r.r[16];
    while (fp && n < room) {
        uint64_t pair[2];
        if (!target_read(t, fp, pair, 16) || !pair[1]) break;
        fps[n] = pair[0]; out[n++] = pair[1];
        if (pair[0] <= fp) break;
        fp = pair[0];
    }
    return n;
}

/* the first address of a function's body (after its prologue row) */
static uint64_t body_of(const Dwarf *d, const char *linkage) {
    for (uint32_t i = 1; i < d->ndies; i++) {
        Val v, lo;
        if (d->dies[i].ab->tag != TAG_subprogram || !die_attr(d, i, 0x6e, &v) || !v.b || strcmp((const char *)v.b, linkage) || !die_attr(d, i, AT_low_pc, &lo)) continue;
        const Row *w = row_at(d, lo.u);
        return w && w + 1 < d->rows + d->nrows && !w->end ? w[1].addr : 0;
    }
    return 0;
}

int session(const char *zig, const char *src) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    Compiler zc;
    double t0 = now();
    if (!zc_start(&zc, zig, src)) { printf("error: first build failed\n"); return 1; }
    printf("built in %.0f ms\n(zdb)\n", (now() - t0) * 1e3);
    char live[64], snap[64];
    int gen = 0;
    snprintf(live, sizeof live, "./zdb-live");
    snprintf(snap, sizeof snap, "./zdb-snap.%d", gen);
    copy_file(zc.out, live); copy_file(zc.out, snap);
    chmod(live, 0755);
    Dwarf d;
    dwarf_open(&d, snap);
    char *text = read_text(src);                 /* the source as the running code was built from it */
    uint32_t lines[64]; int nl = 0;
    uint64_t traps[256]; uint32_t nt = 0;
    Target t;
    char *argv[] = { live, NULL };
    if (!target_start(&t, argv)) { printf("error: cannot start\n"); return 1; }
    char cmd[256];
    Regs view;                                   /* the registers of the frame shown: the top one, or after a panic the user's */
    int top = 0;                                 /* that frame's index */
    target_regs(&t, &view);
    const char *base = strrchr(src, '/') ? strrchr(src, '/') + 1 : src;
    while (fgets(cmd, sizeof cmd, stdin)) {
        cmd[strcspn(cmd, "\n")] = 0;
        if (cmd[0] == 'b' && cmd[1] == ' ') {
            if (nl < 64) lines[nl++] = (uint32_t)atoi(cmd + 2);
        } else if (!strcmp(cmd, "c")) {
            uint64_t panic_at = body_of(&d, "debug.defaultPanic");   /* every safety check and @panic ends here */
            nt = 0;
            if (panic_at) traps[nt++] = panic_at;
            for (int i = 0; i < nl; i++) nt += line_addrs(&d, base, lines[i], traps + nt, 256 - nt);
            uint64_t pc; int code;
            if (!target_run(&t, traps, nt, &pc, &code)) { printf("exit %d\n", code); fflush(stdout); break; }
            target_regs(&t, &view);
            top = 0;
            if (pc == panic_at) {           /* show the user's frame that failed: fix it and :w, or c ends the program */
                Out o = { 0 };
                uint32_t v = var_at(&d, pc, "msg");
                if (v) print_text(&d, &t, &view, v, &o);
                uint64_t fr[64], fp[64];
                int n = frames(&t, fr, fp, 64), i = 1;
                const Row *w = NULL;
                for (; i < n; i++)                 /* the first frame in the user's file */
                    if ((w = row_at(&d, fr[i] - 1)) && file_is(d.files[w->file].name, base)) break;
                uint32_t f = i < n ? func_at(&d, fr[i] - 1) : 0;
                uint64_t size = f ? frame_size(&d, f) : 0;
                if (size) {                      /* its registers as far as memory tells: rsp, rbp, pc */
                    top = i;
                    view.r[7] = fp[i] - size; view.r[6] = fp[i]; view.r[16] = fr[i] - 1;
                    printf("panic: %.*s at line %u in %s\n", (int)o.n, o.s ? o.s : "", w->line, die_name(&d, f) ? die_name(&d, f) : "?");
                } else printf("panic: %.*s outside %s\n", (int)o.n, o.s ? o.s : "", base);
                free(o.s);
            } else {
                const Row *w = row_at(&d, pc);
                uint32_t f = func_at(&d, pc);
                printf("stop %u %s\n", w ? w->line : 0, f && die_name(&d, f) ? die_name(&d, f) : "?");
            }
        } else if (cmd[0] == 'p' && cmd[1] == ' ') {
            Out o = { 0 };
            uint32_t v = var_at(&d, view.r[16], cmd + 2);
            if (!v) v = global_named(&d, cmd + 2);
            if (!v) out_add(&o, "<no variable %s>", cmd + 2); else print_var(&d, &t, &view, v, &o);
            printf("%.*s\n", (int)o.n, o.s ? o.s : "");
            free(o.s);
        } else if (!strcmp(cmd, "bt")) {
            uint64_t fr[64], fp[64];
            int n = frames(&t, fr, fp, 64);
            for (int i = 0; i < n; i++) {
                uint64_t pc = i ? fr[i] - 1 : fr[i];
                const Row *w = row_at(&d, pc);
                uint32_t f = func_at(&d, pc);
                printf("#%d %s:%u\n", i, f && die_name(&d, f) ? die_name(&d, f) : "?", w ? w->line : 0);
            }
        } else if (!strcmp(cmd, "w")) {
            char msg[512];
            double a = now();
            int rc = zc_update(&zc, msg, sizeof msg);
            double b = now();
            if (rc) { printf("compile error: %s\n(zdb)\n", msg); continue; }
            char next[64];
            snprintf(next, sizeof next, "./zdb-snap.%d", gen + 1);
            copy_file(zc.out, next);
            Dwarf nd;
            dwarf_open(&nd, next);
            uint64_t fr[64], fp[64];
            int n = frames(&t, fr, fp, 64);
            for (int i = 1; i < n; i++) fr[i]--;                /* the call, not the instruction after it */
            fr[top] = view.r[16];
            Patch p;
            char *nsrc = read_text(src);
            if (ec_patch(&t, &d, &nd, fr + top, n - top, &view, top > 0, text, nsrc ? nsrc : "", &p)) {
                if (p.new_pc) {
                    target_set_regs(&t, &p.regs);
                    if (p.why[0]) printf("%s\n", p.why);
                    else printf("the stopped function was edited: line %u goes on at line %u\n", p.from_line, p.to_line);
                    view = p.regs; top = 0;
                }
                free(text); text = nsrc;
                dwarf_close(&d); d = nd; gen++;
                printf("patched: %u runs, code %llu, rodata %llu, new data %llu, kept %llu bytes; build %.1f ms, patch %.1f ms\n", p.runs,
                       (unsigned long long)p.code, (unsigned long long)p.rodata, (unsigned long long)p.data, (unsigned long long)p.kept,
                       (b - a) * 1e3, (now() - b) * 1e3);
            } else {
                free(nsrc);
                dwarf_close(&nd);
                printf("refused: %s\n", p.why);
            }
        } else if (!strcmp(cmd, "d")) nl = 0;          /* delete every breakpoint */
        else if (!strcmp(cmd, "q")) break;
        printf("(zdb)\n");
    }
    target_kill(&t);
    zc_stop(&zc);
    dwarf_close(&d);
    return 0;
}
