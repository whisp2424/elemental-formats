#include "idx2.h"

#include <errno.h>
#include <string.h>

#include "common.h"

static void usage(void)
{
    fputs("usage:\n"
          "  idx2 list ARCHIVE\n"
          "  idx2 extract ARCHIVE OUTDIR\n",
          stderr);
    exit(2);
}

int main(int argc, char **argv)
{
    if (argc < 3)
        usage();
    const char *cmd = argv[1];
    int extract = strcmp(cmd, "extract") == 0;
    if (strcmp(cmd, "list") != 0 && !extract)
        usage();
    if (extract && argc < 4)
        usage();

    FILE *f = fopen(argv[2], "rb");
    if (!f)
        die("cannot open %s: %s", argv[2], strerror(errno));
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    uint8_t *data = xmalloc((size_t)sz);
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz)
        die("short read on %s", argv[2]);
    fclose(f);

    Idx2Archive arc;
    if (idx2_load(data, sz, &arc) != 0)
        die("%s is not an IDX2 archive", argv[2]);
    if (extract) {
        idx2_extract(&arc, data, argv[3]);
        printf("extracted %u files\n", arc.count);
    } else {
        idx2_list(&arc, stdout);
    }
    idx2_free(&arc);
    free(data);
    return 0;
}
