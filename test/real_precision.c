/* Text APIs must round-trip binary64 values from both changes and block frames. */
#include "fstapi.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const double values[] = {0x1.0000000000001p0, DBL_MAX, -0x1p-1074, -0.0, -DBL_MAX};
static const uint64_t times[] = {0, 1, 4, 5, 6};
enum { count = sizeof(values) / sizeof(values[0]) };
static int failures;

static void check_value(const char *api, const char *text, double expected)
{
    char *end;
    if (text) {
        if (*text == 'r') ++text;
        double got = strtod(text, &end);
        if (end != text && (*end == '\0' || *end == ' ') &&
            memcmp(&got, &expected, sizeof(got)) == 0) return;
    }
    fprintf(stderr, "%s: expected %a, got %s\n", api, expected, text ? text : "NULL");
    ++failures;
}

static void on_change(void *user, uint64_t time, fstHandle id, const unsigned char *raw)
{
    size_t *i = user;
    (void)time;
    (void)id;
    if (*i < count) check_value("block callback", (const char *)raw, values[*i]);
    ++*i;
}

static void check_streams(void *reader, size_t first)
{
    size_t i = first;
    if (!fstReaderIterBlocks(reader, on_change, &i, NULL) || i != count) ++failures;

    FILE *vcd = tmpfile();
    if (!vcd) { perror("tmpfile"); exit(1); }
    if (!fstReaderIterBlocks(reader, NULL, NULL, vcd)) ++failures;
    rewind(vcd);
    char line[128];
    i = first;
    while (fgets(line, sizeof(line), vcd)) {
        if (line[0] == 'r') {
            if (i < count) check_value("VCD export", line, values[i]);
            ++i;
        }
    }
    if (ferror(vcd) || i != count) ++failures;
    fclose(vcd);
}

int main(void)
{
    const char *path = "real-precision.fst";
    void *writer = fstWriterCreate(path, 1);
    if (!writer) return 1;
    fstWriterSetScope(writer, FST_ST_VCD_MODULE, "top", NULL);
    fstHandle id = fstWriterCreateVar(writer, FST_VT_VCD_REAL, FST_VD_IMPLICIT, 8, "real", 0);
    fstWriterSetUpscope(writer);
    for (size_t i = 0; i < count; ++i) {
        if (i == 2) {
            fstWriterEmitTimeChange(writer, 2);
            fstWriterFlushContext(writer);
            fstWriterEmitTimeChange(writer, 3);
        }
        fstWriterEmitTimeChange(writer, times[i]);
        fstWriterEmitValueChange(writer, id, &values[i]);
    }
    fstWriterClose(writer);

    void *reader = fstReaderOpen(path);
    if (!reader || fstReaderGetValueChangeSectionCount(reader) != 2) return 1;
    char buf[64];
    for (size_t i = 0; i < count; ++i) {
        check_value("random access", fstReaderGetValueFromHandleAtTime(reader, times[i], id, buf),
                    values[i]);
    }
    check_value("random-access frame", fstReaderGetValueFromHandleAtTime(reader, 3, id, buf),
                values[1]);
    fstReaderSetFacProcessMask(reader, id);
    check_streams(reader, 0);
    /* Starting in the second block emits its inherited first frame. */
    fstReaderSetLimitTimeRange(reader, 3, times[count - 1]);
    check_streams(reader, 1);
    fstReaderClose(reader);
    return failures != 0;
}
