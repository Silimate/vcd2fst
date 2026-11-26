/* A hierarchy too large for LZ4 is stored with gzip, and the reader never
 * walks a hierarchy it could not decode, or past the header's var count.
 * Build with -DFST_HDR_FOURPACK_MAX_SIZE=4096 so a small dump crosses the
 * LZ4 limit. */
#include "fstapi.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HIER_GZ 4      /* FST_BL_HIER */
#define HIER_LZ4 6     /* FST_BL_HIER_LZ4 */
#define LZ4_MAX 0x7E000000ULL
#define OFFS_NUM_VARS 49 /* FST_HDR_OFFS_NUM_VARS: after the tag and six 64-bit fields */

static int failures;

static void fail(const char *what, unsigned long long got, unsigned long long want)
{
    fprintf(stderr, "%s: got %llu, want %llu\n", what, got, want);
    ++failures;
}

static void timeout(int sig)
{
    static const char msg[] = "timed out walking the hierarchy\n";
    (void)sig;
    if (write(2, msg, sizeof(msg) - 1)) {}
    _exit(1);
}

/* Write `vars` vars under top.u_<i>; every 4th aliases the var before it.
   `tail` names a last scope, which the truncation case cuts through. */
static void write_dump(const char *path, int pack, int compress, int vars, const char *tail)
{
    void *ctx = fstWriterCreate(path, compress);
    if (!ctx) { perror(path); exit(1); }
    fstWriterSetPackType(ctx, pack);
    fstWriterSetScope(ctx, FST_ST_VCD_MODULE, "top", NULL);
    char name[64];
    fstHandle last = 0;
    for (int i = 0; i < vars; i++) {
        if (i % 100 == 0) {
            if (i) fstWriterSetUpscope(ctx);
            snprintf(name, sizeof(name), "u_%d", i / 100);
            fstWriterSetScope(ctx, FST_ST_VCD_MODULE, name, NULL);
        }
        snprintf(name, sizeof(name), "sig_%d", i);
        fstHandle h = fstWriterCreateVar(ctx, FST_VT_VCD_WIRE, FST_VD_IMPLICIT, 1, name,
                                         i % 4 == 3 ? last : 0);
        if (i % 4 != 3) last = h;
    }
    fstWriterSetUpscope(ctx);
    if (tail) {
        fstWriterSetScope(ctx, FST_ST_VCD_MODULE, tail, NULL);
        fstWriterSetUpscope(ctx);
    }
    fstWriterSetUpscope(ctx);
    for (uint64_t t = 0; t < 2; t++) {
        fstWriterEmitTimeChange(ctx, t);
        for (fstHandle h = 1; h <= last; h++) fstWriterEmitValueChange(ctx, h, t ? "1" : "0");
    }
    fstWriterClose(ctx);
}

static uint64_t be64(FILE *f)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | (unsigned)fgetc(f);
    return v;
}

/* Tag of the hierarchy block; *uclen_at gets the offset of its length field. */
static int hier_tag(const char *path, long *uclen_at)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    long pos = 0;
    int tag;
    while ((tag = fgetc(f)) != EOF) {
        uint64_t len = be64(f);
        if (tag == HIER_GZ || tag == HIER_LZ4 || tag == HIER_LZ4 + 1) {
            *uclen_at = pos + 9;
            fclose(f);
            return tag;
        }
        if (!len) break;
        pos += 1 + (long)len;
        fseek(f, pos, SEEK_SET);
    }
    fclose(f);
    return -1;
}

static void patch64(const char *path, long at, uint64_t v)
{
    FILE *f = fopen(path, "r+b");
    if (!f) { perror(path); exit(1); }
    fseek(f, at, SEEK_SET);
    for (int i = 7; i >= 0; i--) fputc((int)(v >> (8 * i)) & 0xff, f);
    fclose(f);
}

static void copy(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
    if (!a || !b) { perror(from); exit(1); }
    int ch;
    while ((ch = fgetc(a)) != EOF) fputc(ch, b);
    fclose(a);
    fclose(b);
}

/* Walk the hierarchy: returns the var count, checks names, and requires the
   walk to stay ended once it ends. */
static unsigned long long walk(const char *path, int *rewound, unsigned long long *header)
{
    void *ctx = fstReaderOpen(path);
    if (!ctx) { fprintf(stderr, "%s: open failed\n", path); exit(1); }
    *header = fstReaderGetVarCount(ctx);
    *rewound = fstReaderIterateHierRewind(ctx);
    unsigned long long vars = 0;
    struct fstHier *h;
    char want[64];
    while ((h = fstReaderIterateHier(ctx))) {
        if (h->htyp != FST_HT_VAR) continue;
        snprintf(want, sizeof(want), "sig_%llu", vars);
        if (strcmp(h->u.var.name, want)) {
            fprintf(stderr, "%s: var %llu is \"%s\", want \"%s\"\n", path, vars, h->u.var.name, want);
            ++failures;
            break;
        }
        ++vars;
    }
    if (fstReaderIterateHier(ctx)) {
        fprintf(stderr, "%s: the walk resumed after it ended\n", path);
        ++failures;
    }
    fstReaderClose(ctx);
    return vars;
}

int main(void)
{
    signal(SIGALRM, timeout);
    alarm(60);
    int rewound;
    unsigned long long header, vars;
    long at;

    /* Under the limit, LZ4 still packs the hierarchy. */
    write_dump("small.fst", FST_WR_PT_LZ4, 1, 20, NULL);
    if (hier_tag("small.fst", &at) != HIER_LZ4) fail("small LZ4 hierarchy tag", hier_tag("small.fst", &at), HIER_LZ4);
    vars = walk("small.fst", &rewound, &header);
    if (!rewound || vars != 20 || header != 20) fail("small LZ4 vars", vars, 20);

    /* Over it, the writer falls back to gzip, and every var reads back. */
    write_dump("large.fst", FST_WR_PT_LZ4, 1, 1000, NULL);
    if (hier_tag("large.fst", &at) != HIER_GZ) fail("large LZ4 hierarchy tag", hier_tag("large.fst", &at), HIER_GZ);
    vars = walk("large.fst", &rewound, &header);
    if (!rewound || vars != 1000 || header != 1000) fail("large LZ4 vars", vars, 1000);

    /* An older writer's oversized LZ4 section: refused without decoding. */
    copy("small.fst", "oversized.fst");
    hier_tag("oversized.fst", &at);
    patch64("oversized.fst", at, LZ4_MAX + 1);
    vars = walk("oversized.fst", &rewound, &header);
    if (rewound || vars) fail("oversized LZ4 section vars", vars, 0);

    /* A section that doesn't decode to its stated length is not walked. */
    copy("small.fst", "short.fst");
    {
        FILE *f = fopen("short.fst", "rb");
        hier_tag("short.fst", &at);
        fseek(f, at, SEEK_SET);
        uint64_t uclen = be64(f);
        fclose(f);
        patch64("short.fst", at, uclen + 64);
    }
    vars = walk("short.fst", &rewound, &header);
    if (rewound || vars) fail("short LZ4 section vars", vars, 0);

    /* The walk stops at the header's var count. */
    copy("large.fst", "overcount.fst");
    patch64("overcount.fst", OFFS_NUM_VARS, 990);
    vars = walk("overcount.fst", &rewound, &header);
    if (header != 990 || vars != 990) fail("vars past the header count", vars, 990);

    /* EOF inside a name ends it rather than spinning. */
    write_dump("cut.fst", FST_WR_PT_LZ4, 0, 20, "u_scope_cut_short_by_eof");
    {
        FILE *f = fopen("cut.fst.hier", "rb");
        if (!f) { perror("cut.fst.hier"); return 1; }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fclose(f);
        if (truncate("cut.fst.hier", len - 16)) { perror("truncate"); return 1; }
    }
    vars = walk("cut.fst", &rewound, &header);
    if (vars != 20) fail("truncated hierarchy vars", vars, 20);

    if (failures) return 1;
    puts("hier_lz4_limit: ok");
    return 0;
}
