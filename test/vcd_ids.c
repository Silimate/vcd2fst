/* vcd2fst's id map (vcdid.h) finds every declared id, whatever the scheme;
 * a VCD in each scheme converts and reads back with the right handles,
 * aliases, lengths and values; and converting takes well under 120 bytes of
 * memory per signal (a tree node per id took about 200).
 * Usage: vcd_ids path/to/vcd2fst */
#include "fstapi.h"
#include "vcdid.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define SIGS 20000 /* past 3-char ids, and through several table growths */
#define TIMES 4

static int failures;

static void fail(const char *what, unsigned long long got, unsigned long long want)
{
    fprintf(stderr, "%s: got %llu, want %llu\n", what, got, want);
    ++failures;
}

/* bijective base-94, first char least significant, as fst2vcd writes ids */
static int bij(char *buf, uint64_t n)
{
    char *p = buf;
    while (n) {
        n--;
        *p++ = (char)('!' + n % 94);
        n /= 94;
    }
    *p = 0;
    return (int)(p - buf);
}

/* plain base-94 from 1, which many simulators write */
static int plain(char *buf, uint64_t n)
{
    char *p = buf;
    do {
        *p++ = (char)('!' + n % 94);
        n /= 94;
    } while (n);
    *p = 0;
    return (int)(p - buf);
}

/* the id of signal i (from 0) in each scheme: dense, plain, mixed, and
   sparse (numbers 8 apart, past the first dir, then far ones) */
static void ident(char *buf, int scheme, int i)
{
    if (scheme == 0) {
        bij(buf, i + 1);
    } else if (scheme == 1) {
        plain(buf, i + 1);
    } else if (scheme == 3) {
        bij(buf, i < SIGS / 2 ? 8ULL * i + 5 : 94ULL * 94 * 94 * 94 * 94 + i);
    } else if (i % 4 == 0) {
        sprintf(buf, "longid_%06d", i); /* too long for a number */
    } else if (i % 4 == 1) {
        sprintf(buf, "\x7f%d", i); /* outside '!'..'~' */
    } else if (i % 4 == 2) {
        bij(buf, 94ULL * 94 * 94 * 94 * 94 + i); /* 5 chars, past 32 bits */
    } else {
        plain(buf, i + 1);
    }
}

static void unit(void)
{
    char a[16];
    uint64_t p4 = 94ULL * 94 * 94 * 94;

    if (vcdid_code("!", 1) != 1) fail("code of !", vcdid_code("!", 1), 1);
    if (vcdid_code("~", 1) != 94) fail("code of ~", vcdid_code("~", 1), 94);
    if (vcdid_code("!!", 2) != 95) fail("code of !!", vcdid_code("!!", 2), 95);
    bij(a, p4 * 94 + 7); /* 5 chars, past 32 bits */
    if (vcdid_code(a, strlen(a)) != p4 * 94 + 7) fail("5-char code", vcdid_code(a, strlen(a)), p4 * 94 + 7);
    if (vcdid_code("~~~~~~~~~", 9) == 0) fail("9-char code", 0, 1);
    if (vcdid_code("~~~~~~~~~~", 10) != 0) fail("10-char code", 1, 0);
    if (vcdid_code("a b", 3) || vcdid_code("\x7f", 1) || vcdid_code("\x80", 1) || vcdid_code("", 0))
        fail("code of an id outside '!'..'~'", 1, 0);

    /* ids counting up keep no table, and aliases find their handle */
    struct vcdid m = {0};
    for (int i = 1; i <= SIGS; i++) {
        bij(a, i);
        if (vcdid_find(&m, a, strlen(a))) fail("dense: found before add", i, 0);
        if (vcdid_add(&m, a, strlen(a), i % 7) != (uint32_t)i) fail("dense: handle", m.count, i);
    }
    if (m.dir || m.keys) fail("dense: kept a dir or keys", 1, 0);
    for (int i = 1; i <= SIGS; i++) {
        bij(a, i);
        if (vcdid_find(&m, a, strlen(a)) != (uint32_t)i) fail("dense: find", vcdid_find(&m, a, strlen(a)), i);
        if (m.lens[i] != (uint32_t)(i % 7)) fail("dense: len", m.lens[i], i % 7);
    }
    bij(a, SIGS + 1);
    if (vcdid_find(&m, a, strlen(a))) fail("dense: found an id never added", 1, 0);

    /* an id without a number moves everything to the table, and all still
       find; "\x7f" and "!!" (95) used to hash alike */
    if (vcdid_add(&m, "\x7f", 1, 3) != SIGS + 1) fail("table: handle", m.count, SIGS + 1);
    if (!m.keys) fail("table: no keys", 0, 1);
    if (vcdid_find(&m, "!!", 2) != 95) fail("table: !!", vcdid_find(&m, "!!", 2), 95);
    for (int i = 1; i <= SIGS; i++) {
        bij(a, i);
        if (vcdid_find(&m, a, strlen(a)) != (uint32_t)i) fail("table: find", vcdid_find(&m, a, strlen(a)), i);
    }
    if (vcdid_find(&m, "\x7f", 1) != SIGS + 1) fail("table: string id", vcdid_find(&m, "\x7f", 1), SIGS + 1);
    if (vcdid_find(&m, "\x7f\x7f", 2) || vcdid_find(&m, "\x7e", 1) != 94) fail("table: near misses", 1, 0);
    vcdid_free(&m);

    /* counting up, then out of order (dir), then a far number (table) */
    static char seq[3 * SIGS][16];
    int n = 0;
    for (; n < 1000; n++) bij(seq[n], n + 1);
    for (int i = 0; i < SIGS; i++) bij(seq[n++], 1000 + 4 * (i ^ 1) + 1); /* pairs swapped, past the first dir */
    bij(seq[n++], p4 * p4); /* 8 chars, too far for the dir */
    for (int i = 0; i < SIGS; i++) bij(seq[n++], 10 * SIGS + 3 * i); /* back to small numbers */
    for (int i = 0; i < n; i++) {
        if (vcdid_find(&m, seq[i], strlen(seq[i]))) fail("seq: found before add", i, 0);
        if (vcdid_add(&m, seq[i], strlen(seq[i]), i) != (uint32_t)i + 1) fail("seq: handle", m.count, i + 1);
        if (i == 1000 && (!m.dir || m.keys)) fail("seq: dir after the first swap", 0, 1);
        if (i == 1000 + SIGS - 1 && (!m.dir || m.keys)) fail("seq: dir before the far number", 0, 1);
        if (i == 1000 + SIGS && (m.dir || !m.keys)) fail("seq: table after the far number", 0, 1);
        if (i == 1000 + SIGS - 1 || i == n - 1) {
            for (int j = 0; j <= i; j++) {
                uint32_t h = vcdid_find(&m, seq[j], strlen(seq[j]));
                if (h != (uint32_t)j + 1 || m.lens[h] != (uint32_t)j) {
                    fail("seq: find", h, j + 1);
                    break;
                }
            }
        }
    }
    vcdid_free(&m);

    /* the other schemes, from the start */
    for (int scheme = 1; scheme <= 2; scheme++) {
        struct vcdid t = {0};
        for (int i = 0; i < SIGS; i++) {
            ident(a, scheme, i);
            if (vcdid_add(&t, a, strlen(a), 1) != (uint32_t)i + 1) fail("scheme: handle", t.count, i + 1);
        }
        for (int i = 0; i < SIGS; i++) {
            ident(a, scheme, i);
            if (vcdid_find(&t, a, strlen(a)) != (uint32_t)i + 1) fail("scheme: find", vcdid_find(&t, a, strlen(a)), i + 1);
        }
        /* plain ids fit the dir, and never end in '!' */
        if (scheme == 1 && (!t.dir || t.keys || vcdid_find(&t, "!", 1) || vcdid_find(&t, "!!", 2)))
            fail("scheme: plain ids in the dir, gaps not found", 1, 0);
        if (scheme == 2 && (!t.keys || vcdid_find(&t, "longid_00000", 12) || vcdid_find(&t, "longid_0000000", 14) ||
                            vcdid_find(&t, "longid_000000", 13) != 1))
            fail("scheme: string id and its prefix or extension", 1, 0);
        vcdid_free(&t);
    }
}

static void expect(int i, int k, char *out)
{
    unsigned v = (unsigned)((i * 2654435761u + k * 40503u) >> 7);
    if (i % 10 == 9) {
        for (int q = 0; q < 8; q++) out[q] = (char)('0' + ((v >> (7 - q)) & 1));
        out[8] = 0;
    } else {
        out[0] = "01xz01"[v % 6];
        out[1] = 0;
    }
}

static char vals[SIGS + 1][TIMES][9];

static void collect(void *u, uint64_t time, fstHandle h, const unsigned char *v)
{
    (void)u;
    if (h < 1 || h > SIGS || time >= TIMES) {
        ++failures;
        return;
    }
    int w = (h - 1) % 10 == 9 ? 8 : 1;
    for (uint64_t k = time; k < TIMES; k++) {
        memcpy(vals[h][k], v, w);
        vals[h][k][w] = 0;
    }
}

static const char *names[] = {"dense", "plain", "mixed", "sparse"};

/* sigs signals in scopes of 100, each with an alias of its first */
static void write_vcd(const char *vcd, int scheme, int sigs, int times)
{
    char id[32], id0[32];
    FILE *f = fopen(vcd, "w");
    if (!f) {
        perror(vcd);
        exit(1);
    }
    fprintf(f, "$date\n\ttoday\n$end\n$timescale 1ps $end\n$scope module top $end\n");
    for (int i = 0; i < sigs; i++) {
        ident(id, scheme, i);
        if (i % 100 == 0) {
            fprintf(f, "$scope module s%d $end\n", i / 100);
            strcpy(id0, id);
        }
        if (i % 10 == 9) fprintf(f, "$var wire 8 %s w%d [7:0] $end\n", id, i % 100);
        else fprintf(f, "$var wire 1 %s w%d $end\n", id, i % 100);
        if (i % 100 == 99) fprintf(f, "$var wire 1 %s a0 $end\n$upscope $end\n", id0);
    }
    fprintf(f, "$upscope $end\n$enddefinitions $end\n");
    for (int k = 0; k < times; k++) {
        fprintf(f, "#%d\n", k);
        for (int i = 0; i < sigs; i++) {
            char v[9];
            const char *blank = scheme == 2 && i % 3 == 0 ? (i % 2 ? " " : "\t") : "";
            expect(i, k, v);
            ident(id, scheme, i);
            if (i % 10 == 9) fprintf(f, "b%s %s%s\n", v + strspn(v, "0") - (v[strspn(v, "0")] == 0), id, blank);
            else fprintf(f, "%s%s%s\n", v, id, blank);
        }
    }
    fclose(f);
}

/* writes a VCD in this scheme, converts it, and reads it back */
static void roundtrip(const char *bin, int scheme)
{
    char vcd[64], fst[64], cmd[512], line[256];
    snprintf(vcd, sizeof(vcd), "ids_%s.vcd", names[scheme]);
    snprintf(fst, sizeof(fst), "ids_%s.fst", names[scheme]);
    write_vcd(vcd, scheme, SIGS, TIMES);

    snprintf(cmd, sizeof(cmd), "'%s' %s %s 2>&1", bin, vcd, fst);
    FILE *p = popen(cmd, "r");
    char want[128];
    snprintf(want, sizeof(want), "vcd2fst: %d vars, %d aliases, last time %d\n", SIGS, SIGS / 100, TIMES - 1);
    int said = 0;
    while (fgets(line, sizeof(line), p)) {
        if (!strcmp(line, want)) said = 1;
        else fprintf(stderr, "%s: %s", names[scheme], line);
    }
    int rc = pclose(p);
    if (rc || !said) fail(names[scheme], (unsigned long long)rc, 0);

    void *ctx = fstReaderOpen(fst);
    if (!ctx) {
        fail("open", 0, 1);
        return;
    }
    if (fstReaderGetMaxHandle(ctx) != SIGS) fail("max handle", fstReaderGetMaxHandle(ctx), SIGS);
    if (fstReaderGetVarCount(ctx) != SIGS + SIGS / 100) fail("var count", fstReaderGetVarCount(ctx), SIGS + SIGS / 100);
    struct fstHier *hier;
    int scope = 0, wrong = 0;
    while ((hier = fstReaderIterateHier(ctx))) {
        if (hier->htyp == FST_HT_SCOPE && hier->u.scope.name[0] == 's') scope = atoi(hier->u.scope.name + 1);
        if (hier->htyp != FST_HT_VAR) continue;
        int j = hier->u.var.name[0] == 'a' ? 0 : atoi(hier->u.var.name + 1);
        unsigned h = scope * 100 + j + 1;
        unsigned len = (h - 1) % 10 == 9 ? 8 : 1;
        if (hier->u.var.handle != h || hier->u.var.length != len ||
            hier->u.var.is_alias != (hier->u.var.name[0] == 'a'))
            wrong++;
    }
    if (wrong) fail("vars with a wrong handle, length or alias", wrong, 0);
    memset(vals, 0, sizeof(vals));
    fstReaderSetFacProcessMaskAll(ctx);
    fstReaderIterBlocks(ctx, collect, NULL, NULL);
    int bad = 0;
    for (int i = 0; i < SIGS; i++)
        for (int k = 0; k < TIMES; k++) {
            char v[9];
            expect(i, k, v);
            if (strcmp(vals[i + 1][k], v)) {
                if (bad++ < 3) fprintf(stderr, "%s: signal %d at %d is '%s', want '%s'\n", names[scheme], i, k, vals[i + 1][k], v);
            }
        }
    if (bad) fail("wrong values", bad, 0);
    fstReaderClose(ctx);
    remove(vcd);
    remove(fst);
}

/* peak resident bytes of converting a VCD of this many signals */
static double peak(const char *bin, int scheme, int sigs)
{
    char vcd[64], fst[64];
    snprintf(vcd, sizeof(vcd), "peak_%d.vcd", sigs);
    snprintf(fst, sizeof(fst), "peak_%d.fst", sigs);
    write_vcd(vcd, scheme, sigs, 1);
    pid_t pid = fork();
    if (!pid) {
        int null = open("/dev/null", O_WRONLY);
        dup2(null, 1);
        dup2(null, 2);
        execl(bin, bin, vcd, fst, (char *)NULL);
        _exit(127);
    }
    int st = 0;
    struct rusage ru;
    if (wait4(pid, &st, 0, &ru) != pid || !WIFEXITED(st) || WEXITSTATUS(st)) fail("peak: convert", st, 0);
    remove(vcd);
    remove(fst);
#ifdef __APPLE__
    return (double)ru.ru_maxrss; /* bytes */
#else
    return ru.ru_maxrss * 1024.0; /* kilobytes */
#endif
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: vcd_ids path/to/vcd2fst\n");
        return 2;
    }
    unit();
    for (int scheme = 0; scheme < 4; scheme++) roundtrip(argv[1], scheme);
    for (int scheme = 0; scheme < 3; scheme++) {
        double small = peak(argv[1], scheme, 50000), big = peak(argv[1], scheme, 450000);
        double per = (big - small) / 400000;
        printf("vcd_ids: %s ids take %.0f bytes a signal (%.1f MB at 50k signals, %.1f MB at 450k)\n",
               names[scheme], per, small / 1e6, big / 1e6);
        if (per > 120) fail("bytes a signal", (unsigned long long)per, 120);
    }
    if (failures) {
        fprintf(stderr, "vcd_ids: %d failure(s)\n", failures);
        return 1;
    }
    printf("vcd_ids: ok\n");
    return 0;
}
