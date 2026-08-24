#include "imgin.h"

#include "common.h"

#include <string.h>

static uint8_t *read_whole(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        die("cannot open %s", path);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    uint8_t *buf = xmalloc((size_t)sz);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz)
        die("short read on %s", path);
    fclose(f);
    *len = (size_t)sz;
    return buf;
}

static uint8_t *pam_read(const uint8_t *d, size_t len, uint32_t *w,
                         uint32_t *h, int *has_alpha)
{
    size_t pos = 3;
    uint32_t width = 0, height = 0, depth = 0, maxval = 0;
    int alpha = 0;
    char line[256];
    while (pos < len) {
        size_t start = pos;
        while (pos < len && d[pos] != '\n')
            pos++;
        if (pos >= len || (size_t)(pos - start) >= sizeof line)
            return NULL;
        memcpy(line, d + start, pos - start);
        line[pos - start] = '\0';
        pos++;
        if (line[0] == '#')
            continue;
        if (!strncmp(line, "ENDHDR", 6))
            break;
        if (sscanf(line, "WIDTH %u", &width) == 1 ||
            sscanf(line, "HEIGHT %u", &height) == 1 ||
            sscanf(line, "DEPTH %u", &depth) == 1 ||
            sscanf(line, "MAXVAL %u", &maxval) == 1) {
            continue;
        }
        if (!strncmp(line, "TUPLTYPE", 8)) {
            if (strstr(line, "ALPHA"))
                alpha = 1;
        }
    }
    if (!width || !height || (depth != 3 && depth != 4) || maxval != 255)
        return NULL;
    if ((alpha && depth != 4) || (!alpha && depth != 3))
        return NULL;
    size_t need = (size_t)width * height * depth;
    if (len - pos < need)
        return NULL;
    uint8_t *rgba = xmalloc((size_t)width * height * 4);
    for (size_t i = 0; i < (size_t)width * height; i++) {
        memcpy(rgba + i * 4, d + pos + i * depth, 3);
        rgba[i * 4 + 3] = depth == 4 ? d[pos + i * 4 + 3] : 255;
    }
    *has_alpha = depth == 4;
    *w = width;
    *h = height;
    return rgba;
}

static uint8_t *bmp_read(const uint8_t *d, size_t len, uint32_t *w,
                         uint32_t *h, int *has_alpha)
{
    if (len < 54 || d[0] != 'B' || d[1] != 'M')
        return NULL;
    uint32_t off_bits = le32(d + 10);
    uint32_t hdr_size = le32(d + 14);
    int32_t sw = (int32_t)le32(d + 18);
    int32_t sh = (int32_t)le32(d + 22);
    uint16_t bpp = d[28] | d[29] << 8;
    uint32_t compression = le32(d + 30);
    if (bpp != 24 && bpp != 32)
        return NULL;
    if (compression != 0 && compression != 3)
        return NULL;

    uint32_t width = (uint32_t)sw;
    uint32_t height = sh < 0 ? (uint32_t)(-sh) : (uint32_t)sh;
    int top_down = sh < 0;
    size_t stride = ((size_t)width * bpp / 8 + 3) & ~(size_t)3;
    if (!height || stride > len || off_bits > len ||
        stride * (height - 1) + (uint64_t)off_bits + (size_t)width * (bpp / 8) >
            len)
        return NULL;

    int alpha_ch = 0;
    if (bpp == 32 && compression == 3 && hdr_size >= 52 && le32(d + 66))
        alpha_ch = 1;

    uint8_t *rgba = xmalloc((size_t)width * height * 4);
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *row = d + off_bits + (size_t)y * stride;
        for (uint32_t x = 0; x < width; x++) {
            const uint8_t *s = row + (size_t)x * (bpp / 8);
            uint8_t *dst = rgba +
                ((size_t)(top_down ? y : height - 1 - y) * width + x) * 4;
            dst[0] = s[2];
            dst[1] = s[1];
            dst[2] = s[0];
            dst[3] = alpha_ch ? s[3] : 255;
        }
    }
    *w = width;
    *h = height;
    *has_alpha = alpha_ch;
    return rgba;
}

uint8_t *image_read(const char *path, uint32_t *w, uint32_t *h, int *has_alpha)
{
    size_t len;
    uint8_t *d = read_whole(path, &len);
    uint8_t *img = NULL;
    int alpha = 0;
    if (len > 3 && !memcmp(d, "P7", 2))
        img = pam_read(d, len, w, h, &alpha);
    else
        img = bmp_read(d, len, w, h, &alpha);
    free(d);
    if (!img)
        die("%s: unsupported image (need PAM P7 or a plain BMP)", path);
    *has_alpha = alpha;
    return img;
}
