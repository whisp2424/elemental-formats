#include "imgout.h"

#include "common.h"

static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void chunk_write(FILE *f, const char *type, const uint8_t *data,
                        size_t len)
{
    uint8_t hdr[8], crcb[4];
    be32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);

    uint32_t rolling = 0xFFFFFFFFu;
    for (int i = 0; i < 4; i++)
        rolling = crc_table[(rolling ^ (uint8_t)type[i]) & 0xFF] ^
                  (rolling >> 8);
    for (size_t i = 0; i < len; i++)
        rolling = crc_table[(rolling ^ data[i]) & 0xFF] ^ (rolling >> 8);
    be32(crcb, rolling ^ 0xFFFFFFFFu);
    fwrite(crcb, 1, 4, f);
}

static uint32_t adler32(const uint8_t *buf, size_t len)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + buf[i]) % 65521;
        b = (b + a) % 65521;
    }
    return b << 16 | a;
}

int png_write(FILE *f, uint32_t w, uint32_t h, const uint8_t *rgba_topdown,
              int has_alpha)
{
    static int inited;
    if (!inited) {
        crc_init();
        inited = 1;
    }

    size_t bpp = has_alpha ? 4 : 3;
    size_t stride = (size_t)w * bpp;
    size_t raw_len = (stride + 1) * h;
    uint8_t *raw = xmalloc(raw_len);
    for (uint32_t y = 0; y < h; y++) {
        raw[y * (stride + 1)] = 0;
        const uint8_t *src = rgba_topdown + (size_t)y * w * 4;
        uint8_t *dst = raw + y * (stride + 1) + 1;
        if (has_alpha)
            memcpy(dst, src, stride);
        else
            for (uint32_t x = 0; x < w; x++) {
                dst[x * 3] = src[x * 4];
                dst[x * 3 + 1] = src[x * 4 + 1];
                dst[x * 3 + 2] = src[x * 4 + 2];
            }
    }

    size_t zlen = raw_len + 5 * (raw_len / 65535 + 1) + 6;
    uint8_t *z = xmalloc(zlen);
    size_t zp = 0;
    z[zp++] = 0x78;
    z[zp++] = 0x01;
    size_t off = 0;
    do {
        size_t block = raw_len - off;
        int last = block <= 65535;
        if (block > 65535)
            block = 65535;
        z[zp++] = (uint8_t)(last ? 1 : 0);
        z[zp++] = (uint8_t)(block & 0xFF);
        z[zp++] = (uint8_t)(block >> 8);
        z[zp++] = (uint8_t)(~block & 0xFF);
        z[zp++] = (uint8_t)((~block >> 8) & 0xFF);
        memcpy(z + zp, raw + off, block);
        zp += block;
        off += block;
    } while (off < raw_len);
    uint8_t adler[4];
    be32(adler, adler32(raw, raw_len));
    memcpy(z + zp, adler, 4);
    zp += 4;

    uint8_t ihdr[13];
    be32(ihdr, w);
    be32(ihdr + 4, h);
    ihdr[8] = 8;
    ihdr[9] = has_alpha ? 6 : 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;

    static const uint8_t sig[8] = { 137, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    fwrite(sig, 1, 8, f);
    chunk_write(f, "IHDR", ihdr, sizeof ihdr);
    chunk_write(f, "IDAT", z, zp);
    chunk_write(f, "IEND", NULL, 0);

    free(raw);
    free(z);
    return ferror(f) ? -1 : 0;
}
