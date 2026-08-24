#include "idx2.h"

#include <sys/stat.h>

#include "common.h"

#define IDX2_TABLE_OFFSET 0x420
#define IDX2_RECORD_SIZE 48

static void mkdir_p(const char *path)
{
    char buf[8192];
    size_t n = strlen(path);
    if (n >= sizeof buf)
        die("directory name too long: %s", path);
    snprintf(buf, sizeof buf, "%s", path);
    while (n > 1 && buf[n - 1] == '/')
        buf[--n] = '\0';
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0777);
            *p = '/';
        }
    }
    mkdir(buf, 0777);
}

int idx2_load(const uint8_t *data, size_t len, Idx2Archive *arc)
{
    if (len < IDX2_TABLE_OFFSET || memcmp(data, "IDX2", 4) != 0)
        return -1;
    uint16_t count = le16(data + 6);
    uint32_t declared_size = le32(data + 8);
    if ((uint64_t)declared_size != len) {
        fprintf(stderr,
                "eldump: warning: idx size %u != file size %zu\n",
                declared_size, len);
    }
    arc->count = count;
    arc->entries = xcalloc(count, sizeof(Idx2Entry));
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *rec = data + IDX2_TABLE_OFFSET + (size_t)i * IDX2_RECORD_SIZE;
        Idx2Entry *e = &arc->entries[i];
        e->offset = le32(rec);
        e->size = le32(rec + 4);
        e->tag = le32(rec + 8);
        size_t j;
        for (j = 0; j < 32 && rec[16 + j]; j++)
            e->name[j] = (char)rec[16 + j];
        e->name[j] = '\0';
        if ((uint64_t)e->offset + e->size > len)
            die("entry %s extends past end of archive", e->name);
    }
    return 0;
}

void idx2_free(Idx2Archive *arc)
{
    free(arc->entries);
}

void idx2_list(const Idx2Archive *arc, FILE *out)
{
    for (uint32_t i = 0; i < arc->count; i++) {
        const Idx2Entry *e = &arc->entries[i];
        fprintf(out, "%6u  %10u  %10u  %u  %s\n", i, e->offset, e->size,
                e->tag, e->name);
    }
}

void idx2_extract(const Idx2Archive *arc, const uint8_t *data,
                  const char *outdir)
{
    mkdir_p(outdir);
    for (uint32_t i = 0; i < arc->count; i++) {
        const Idx2Entry *e = &arc->entries[i];
        char path[4096];
        if (snprintf(path, sizeof path, "%s/%s", outdir, e->name) >=
            (int)sizeof path)
            die("output path too long: %s/%s", outdir, e->name);
        FILE *f = fopen(path, "wb");
        if (!f)
            die("cannot create %s", path);
        if (fwrite(data + e->offset, 1, e->size, f) != e->size)
            die("short write on %s", path);
        fclose(f);
    }
}
