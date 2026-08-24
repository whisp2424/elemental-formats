#ifndef ELDUMP_IDX2_H
#define ELDUMP_IDX2_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define IDX2_TAG_BINARY 0
#define IDX2_TAG_TEXT_SOURCE 2

typedef struct {
    char name[33];
    uint32_t offset;
    uint32_t size;
    uint32_t tag;
} Idx2Entry;

typedef struct {
    Idx2Entry *entries;
    uint32_t count;
} Idx2Archive;

int idx2_load(const uint8_t *data, size_t len, Idx2Archive *arc);
void idx2_free(Idx2Archive *arc);
void idx2_list(const Idx2Archive *arc, FILE *out);
void idx2_extract(const Idx2Archive *arc, const uint8_t *data,
                  const char *outdir);

#endif
