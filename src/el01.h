#ifndef ELDUMP_EL01_H
#define ELDUMP_EL01_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define EL_ENC_RAW 0x00
#define EL_ENC_RLE 0x04
#define EL_ENC_PAL_RLE 0x08
#define EL_ENC_GRAY 0x0C
#define EL_ENC_LZSS_32 0x20
#define EL_ENC_PLANAR 0x24

#define EL_FLAG_DELTA_H 0x01
#define EL_FLAG_DELTA_V 0x02
#define EL_FLAG_HAS_ALPHA 0x80

#define EL_MASK_RAW 0x10
#define EL_MASK_RLE 0x11
#define EL_MASK_PAL_RLE 0x12

typedef struct {
    uint8_t flags;
    uint8_t depth;
    uint8_t npal;
    uint8_t esc;
    uint16_t w, h;
    uint32_t extent;
    const uint8_t *body;
    size_t body_len;
} ElStream;

typedef struct {
    uint16_t w, h;
    uint32_t *surface;
    uint8_t *mask;
} ElImageRaw;

int el01_decode_raw(const uint8_t *data, size_t len, ElImageRaw *img);
uint8_t *el01_compose(const ElImageRaw *img);
void el01_raw_free(ElImageRaw *img);

int el01_write(FILE *out, uint32_t w, uint32_t h,
               const uint8_t *rgba_topdown, int with_mask);

#endif
