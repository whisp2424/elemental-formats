#include "el01.h"

#include "common.h"

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} Rd;

static int rd_u8(Rd *r, uint8_t *out)
{
    if (r->pos >= r->len)
        return 0;
    *out = r->data[r->pos++];
    return 1;
}

static void put_pixel(uint32_t *surf, size_t cap, size_t *n, uint32_t px)
{
    if (*n < cap)
        surf[(*n)++] = px & 0x00FFFFFFu;
}

static void delta_horizontal(uint32_t *surf, size_t w, size_t h)
{
    for (size_t y = 0; y < h; y++) {
        uint32_t *row = surf + y * w;
        for (size_t x = 1; x < w; x++)
            row[x] = (row[x] ^ row[x - 1]) & 0x00FFFFFFu;
    }
}

static void delta_vertical(uint32_t *surf, size_t w, size_t h)
{
    for (size_t y = 1; y < h; y++) {
        uint32_t *row = surf + y * w;
        const uint32_t *above = row - w;
        for (size_t x = 0; x < w; x++)
            row[x] = (row[x] ^ above[x]) & 0x00FFFFFFu;
    }
}

static void decode_raw24(const uint8_t *body, size_t len, uint32_t *surf,
                         size_t cap, uint16_t w, uint16_t h)
{
    size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3;
    size_t n = 0;
    for (size_t y = 0; y < h && n < cap; y++) {
        if (y * stride + (size_t)w * 3 > len)
            break;
        const uint8_t *row = body + y * stride;
        for (size_t x = 0; x < w && n < cap; x++, row += 3)
            put_pixel(surf, cap, &n,
                      (uint32_t)row[2] << 16 | (uint32_t)row[1] << 8 | row[0]);
    }
}

static void decode_rle(const uint8_t *body, size_t len, uint8_t esc,
                       uint32_t *surf, size_t cap)
{
    Rd r = { body, len, 0 };
    size_t n = 0;
    uint8_t t, g, b;
    while (rd_u8(&r, &t)) {
        if (t != esc) {
            if (!rd_u8(&r, &g) || !rd_u8(&r, &b))
                return;
            put_pixel(surf, cap, &n, (uint32_t)b << 16 | (uint32_t)g << 8 | t);
            continue;
        }
        uint8_t second;
        if (!rd_u8(&r, &second))
            return;
        if (second == 0xFF) {
            if (!rd_u8(&r, &g) || !rd_u8(&r, &b))
                return;
            put_pixel(surf, cap, &n, (uint32_t)b << 16 | (uint32_t)g << 8 | esc);
        } else if (second == 0x00) {
            return;
        } else {
            uint8_t bb, gg, rr;
            if (!rd_u8(&r, &bb) || !rd_u8(&r, &gg) || !rd_u8(&r, &rr))
                return;
            uint32_t px = (uint32_t)rr << 16 | (uint32_t)gg << 8 | bb;
            for (uint8_t i = 0; i < second; i++)
                put_pixel(surf, cap, &n, px);
        }
    }
}

static void decode_pal_rle(const uint8_t *body, size_t len, uint8_t esc,
                           uint8_t npal, uint32_t *surf, size_t cap)
{
    if ((size_t)npal * 4 > len)
        return;
    uint32_t pal[256];
    for (int i = 0; i < npal; i++)
        pal[i] = (uint32_t)body[i * 4 + 2] << 16 |
                 (uint32_t)body[i * 4 + 1] << 8 | body[i * 4];
    Rd r = { body + (size_t)npal * 4, len - (size_t)npal * 4, 0 };
    size_t n = 0;
    uint8_t t, g, b;
    while (rd_u8(&r, &t)) {
        if (t != esc) {
            if (!rd_u8(&r, &g) || !rd_u8(&r, &b))
                return;
            put_pixel(surf, cap, &n, (uint32_t)b << 16 | (uint32_t)g << 8 | t);
            continue;
        }
        uint8_t second;
        if (!rd_u8(&r, &second))
            return;
        if (second == 0xFF) {
            if (!rd_u8(&r, &g) || !rd_u8(&r, &b))
                return;
            put_pixel(surf, cap, &n, (uint32_t)b << 16 | (uint32_t)g << 8 | esc);
        } else if (npal == 1) {
            if (second == 0)
                return;
            for (uint8_t i = 0; i < second; i++)
                put_pixel(surf, cap, &n, pal[0]);
        } else {
            uint8_t count;
            if (!rd_u8(&r, &count))
                return;
            if (second >= npal || count == 0)
                continue;
            for (uint8_t i = 0; i < count; i++)
                put_pixel(surf, cap, &n, pal[second]);
        }
    }
}

static void decode_gray_pure(const uint8_t *body, size_t len, uint8_t esc,
                             uint32_t *surf, size_t cap)
{
    Rd r = { body, len, 0 };
    size_t n = 0;
    uint8_t t, v;
    while (rd_u8(&r, &t)) {
        if (t != esc) {
            put_pixel(surf, cap, &n, t * 0x00010101u);
            continue;
        }
        uint8_t second;
        if (!rd_u8(&r, &second))
            return;
        if (second == 0xFF)
            put_pixel(surf, cap, &n, esc * 0x00010101u);
        else if (second == 0x00)
            return;
        else if (rd_u8(&r, &v))
            for (uint8_t i = 0; i < second; i++)
                put_pixel(surf, cap, &n, v * 0x00010101u);
    }
}

static void decode_gray_hybrid(const uint8_t *body, size_t len, uint8_t esc,
                               uint32_t *surf, size_t cap)
{
    Rd r = { body, len, 0 };
    size_t n = 0;
    uint8_t cur = 0;
    uint8_t t, g, b;
    while (rd_u8(&r, &t)) {
        if (t != esc) {
            if (cur == 0) {
                put_pixel(surf, cap, &n, t * 0x00010101u);
            } else {
                if (!rd_u8(&r, &g) || !rd_u8(&r, &b))
                    return;
                put_pixel(surf, cap, &n,
                          (uint32_t)b << 16 | (uint32_t)g << 8 | t);
            }
            continue;
        }
        uint8_t second;
        if (!rd_u8(&r, &second))
            return;
        if (second == 0x00) {
            return;
        } else if (second == 0x01) {
            if (!rd_u8(&r, &cur))
                return;
        } else if (second == 0xFF) {
            if (cur == 0) {
                put_pixel(surf, cap, &n, esc * 0x00010101u);
            } else {
                if (!rd_u8(&r, &g) || !rd_u8(&r, &b))
                    return;
                put_pixel(surf, cap, &n,
                          (uint32_t)b << 16 | (uint32_t)g << 8 | esc);
            }
        } else if (cur == 0) {
            if (!rd_u8(&r, &g))
                return;
            for (uint8_t i = 0; i < second; i++)
                put_pixel(surf, cap, &n, g * 0x00010101u);
        } else {
            uint8_t bb, gg, rr;
            if (!rd_u8(&r, &bb) || !rd_u8(&r, &gg) || !rd_u8(&r, &rr))
                return;
            uint32_t px = (uint32_t)rr << 16 | (uint32_t)gg << 8 | bb;
            for (uint8_t i = 0; i < second; i++)
                put_pixel(surf, cap, &n, px);
        }
    }
}

static size_t lzss_v1(const uint8_t *in, size_t in_len, uint8_t *out,
                      size_t out_cap)
{
    size_t ip = 1, op = 0;
    while (ip < in_len && op < out_cap) {
        uint8_t t = in[ip++];
        if (t < 8) {
            if (in_len - ip < (size_t)t + 1)
                break;
            memcpy(out + op, in + ip, (size_t)t + 1);
            ip += (size_t)t + 1;
            op += (size_t)t + 1;
        } else {
            if (ip >= in_len)
                break;
            size_t len = (size_t)t - 7;
            size_t dist = in[ip++];
            for (size_t i = 0; i < len && op < out_cap; i++, op++)
                out[op] = out[op - dist];
        }
    }
    return op;
}

static int decode_lzss_32(const ElStream *s, uint32_t *surf, size_t cap)
{
    size_t bytes = cap * 4;
    uint8_t *buf = xcalloc(bytes, 1);
    lzss_v1(s->body, s->body_len, buf, bytes);
    memcpy(surf, buf, bytes);
    free(buf);
    return 1;
}

static int decode_planar(const ElStream *s, uint32_t *surf, size_t cap)
{
    if (cap > SIZE_MAX / 12)
        return 0;
    size_t plane = (size_t)s->w * s->h;
    uint8_t *planes = xcalloc(plane * 3, 1);
    lzss_v1(s->body, s->body_len, planes, plane * 3);
    uint8_t *bytes = (uint8_t *)surf;
    for (int p = 0; p < 3; p++)
        for (size_t i = 0; i < cap; i++)
            bytes[i * 4 + p] = planes[p * plane + i];
    free(planes);
    return 1;
}

static void decode_color_body(const ElStream *s, uint32_t *surf, size_t cap)
{
    memset(surf, 0, cap * sizeof(uint32_t));
    switch (s->flags & 0x2C) {
    case EL_ENC_RAW:
        decode_raw24(s->body, s->body_len, surf, cap, s->w, s->h);
        break;
    case EL_ENC_RLE:
        decode_rle(s->body, s->body_len, s->esc, surf, cap);
        break;
    case EL_ENC_PAL_RLE:
        decode_pal_rle(s->body, s->body_len, s->esc, s->npal, surf, cap);
        break;
    case EL_ENC_GRAY:
        if (s->flags & EL_FLAG_DELTA_H)
            decode_gray_hybrid(s->body, s->body_len, s->esc, surf, cap);
        else
            decode_gray_pure(s->body, s->body_len, s->esc, surf, cap);
        return;
    case EL_ENC_LZSS_32:
        decode_lzss_32(s, surf, cap);
        break;
    case EL_ENC_PLANAR:
        decode_planar(s, surf, cap);
        break;
    default:
        break;
    }
    if (s->flags & EL_FLAG_DELTA_H)
        delta_horizontal(surf, s->w, s->h);
    if (s->flags & EL_FLAG_DELTA_V)
        delta_vertical(surf, s->w, s->h);
}

static void decode_mask(const ElStream *s, uint8_t *mask, size_t cap)
{
    memset(mask, 0, cap);
    Rd r = { s->body, s->body_len, 0 };
    size_t n = 0;
    uint8_t sel = s->flags & 0x1F;

    if (sel == EL_MASK_RAW) {
        size_t take = s->body_len < cap ? s->body_len : cap;
        memcpy(mask, s->body, take);
    } else if (sel == EL_MASK_RLE && s->depth == 8) {
        uint8_t v, c;
        while (n < cap && rd_u8(&r, &v)) {
            if (!rd_u8(&r, &c))
                return;
            if (c == 0)
                return;
            while (c-- && n < cap)
                mask[n++] = v;
        }
    } else if (sel == EL_MASK_RLE) {
        uint8_t t, c;
        while (n < cap && rd_u8(&r, &t)) {
            if (t & 0xF0)
                continue;
            if (!rd_u8(&r, &c))
                return;
            size_t count = c;
            if (count == 0) {
                uint8_t lo, hi;
                if (!rd_u8(&r, &lo) || !rd_u8(&r, &hi))
                    return;
                count = (size_t)lo | (size_t)hi << 8;
                if (count == 0)
                    return;
            }
            uint8_t v = (uint8_t)(t | t << 4);
            while (count-- && n < cap)
                mask[n++] = v;
        }
    } else if (sel == EL_MASK_PAL_RLE) {
        uint8_t pal[256];
        if ((size_t)s->npal > s->body_len)
            return;
        memcpy(pal, s->body, s->npal);
        r.pos = s->npal;
        uint8_t t, c;
        while (n < cap && rd_u8(&r, &t)) {
            int idx = -1;
            for (int i = 0; i < s->npal; i++)
                if (pal[i] == t) {
                    idx = i;
                    break;
                }
            if (idx < 0) {
                mask[n++] = t;
                continue;
            }
            if (!rd_u8(&r, &c))
                return;
            size_t count = c;
            if (count == 0) {
                uint8_t lo, hi;
                if (!rd_u8(&r, &lo) || !rd_u8(&r, &hi))
                    return;
                count = (size_t)lo | (size_t)hi << 8;
                if (count == 0)
                    return;
            }
            if (count > cap - n)
                count = cap - n;
            memset(mask + n, pal[idx], count);
            n += count;
        }
    }
}

static int parse_stream(const uint8_t *data, size_t len, uint32_t offset,
                        uint16_t hdr_extent, ElStream *s)
{
    uint64_t start = (uint64_t)offset + hdr_extent;
    if (start + 20 > len)
        return -1;
    const uint8_t *p = data + start;
    s->flags = p[2];
    s->extent = le32(p + 8);
    s->w = le16(p + 12);
    s->h = le16(p + 14);
    s->depth = p[16];
    s->npal = p[17];
    s->esc = p[18];
    s->body = p + 20;
    s->body_len = (size_t)(len - start - 20);
    if (s->w == 0 || s->h == 0)
        return -1;
    return 0;
}

int el01_decode_raw(const uint8_t *data, size_t len, ElImageRaw *img)
{
    img->surface = NULL;
    img->mask = NULL;
    if (len < 16 || memcmp(data, "EL01", 4) != 0)
        return -1;
    uint16_t n_streams = le16(data + 8);
    uint16_t dir_off = le16(data + 10);
    uint16_t hdr_extent = le16(data + 12);
    if (n_streams < 1 || n_streams > 2)
        return -1;
    if ((uint64_t)dir_off + 4ull * n_streams > len)
        return -1;

    ElStream streams[2];
    for (int i = 0; i < n_streams; i++) {
        uint32_t off = le32(data + dir_off + 4u * i);
        if (parse_stream(data, len, off, hdr_extent, &streams[i]) != 0)
            return -1;
    }

    ElStream *color = &streams[0];
    size_t cap = (size_t)color->w * color->h;
    img->w = color->w;
    img->h = color->h;
    img->surface = xmalloc(cap * sizeof(uint32_t));
    decode_color_body(color, img->surface, cap);

    if (n_streams == 2 && (streams[1].flags & 0x10)) {
        img->mask = xmalloc(cap);
        decode_mask(&streams[1], img->mask, cap);
    }
    return 0;
}

void el01_raw_free(ElImageRaw *img)
{
    free(img->surface);
    free(img->mask);
}

uint8_t *el01_compose(const ElImageRaw *img)
{
    size_t w = img->w, h = img->h;
    uint8_t *rgba = xmalloc(w * h * 4);
    for (size_t y = 0; y < h; y++) {
        const uint32_t *src = img->surface + (h - 1 - y) * w;
        const uint8_t *msk = img->mask ? img->mask + y * w : NULL;
        uint8_t *dst = rgba + y * w * 4;
        for (size_t x = 0; x < w; x++) {
            dst[x * 4 + 0] = (uint8_t)(src[x] >> 16);
            dst[x * 4 + 1] = (uint8_t)(src[x] >> 8);
            dst[x * 4 + 2] = (uint8_t)src[x];
            dst[x * 4 + 3] = msk ? msk[x] : 255;
        }
    }
    return rgba;
}

static uint8_t pick_esc(const uint32_t *px, size_t n, int gray)
{
    size_t freq[256] = { 0 };
    for (size_t i = 0; i < n; i++) {
        if (gray) {
            freq[(px[i] >> 16) & 0xFF]++;
        } else {
            freq[px[i] & 0xFF]++;
            freq[(px[i] >> 8) & 0xFF]++;
            freq[(px[i] >> 16) & 0xFF]++;
        }
    }
    size_t best = ~(size_t)0;
    uint8_t esc = 0;
    for (int v = 0; v < 256; v++)
        if (freq[v] < best) {
            best = freq[v];
            esc = (uint8_t)v;
        }
    return esc;
}

typedef struct {
    uint8_t *buf;
    size_t len, cap;
} Out;

static void out_byte(Out *o, uint8_t b)
{
    if (o->len == o->cap) {
        o->cap = o->cap ? o->cap * 2 : 4096;
        o->buf = realloc(o->buf, o->cap);
        if (!o->buf)
            die("out of memory");
    }
    o->buf[o->len++] = b;
}

static void emit_rle(Out *o, const uint32_t *px, size_t n, uint8_t esc)
{
    size_t i = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && px[i + run] == px[i])
            run++;
        if (run >= 2) {
            while (run) {
                size_t take = run > 254 ? 254 : run;
                out_byte(o, esc);
                out_byte(o, (uint8_t)take);
                out_byte(o, (uint8_t)px[i]);
                out_byte(o, (uint8_t)(px[i] >> 8));
                out_byte(o, (uint8_t)(px[i] >> 16));
                i += take;
                run -= take;
            }
            continue;
        }
        if ((uint8_t)px[i] == esc) {
            out_byte(o, esc);
            out_byte(o, 0xFF);
        } else {
            out_byte(o, (uint8_t)px[i]);
        }
        out_byte(o, (uint8_t)(px[i] >> 8));
        out_byte(o, (uint8_t)(px[i] >> 16));
        i++;
    }
    out_byte(o, esc);
    out_byte(o, 0x00);
}

static void emit_gray_rle(Out *o, const uint32_t *px, size_t n, uint8_t esc)
{
    size_t i = 0;
    while (i < n) {
        uint8_t v = (uint8_t)(px[i] >> 16);
        size_t run = 1;
        while (i + run < n && (uint8_t)(px[i + run] >> 16) == v)
            run++;
        while (run) {
            size_t take = run > 254 ? 254 : run;
            if (take >= 2) {
                out_byte(o, esc);
                out_byte(o, (uint8_t)take);
                out_byte(o, v);
            } else if (v == esc) {
                out_byte(o, esc);
                out_byte(o, 0xFF);
            } else {
                out_byte(o, v);
            }
            i += take;
            run -= take;
        }
    }
    out_byte(o, esc);
    out_byte(o, 0x00);
}

static int is_gray(const uint32_t *px, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (((px[i] >> 16) & 0xFF) != (px[i] & 0xFF) ||
            ((px[i] >> 8) & 0xFF) != (px[i] & 0xFF))
            return 0;
    return 1;
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void write_stream_header(uint8_t *p, uint8_t flags, uint32_t extent,
                                uint16_t w, uint16_t h, uint8_t depth,
                                uint8_t npal, uint8_t esc)
{
    memset(p, 0, 20);
    p[2] = flags;
    put_u32(p + 8, extent);
    put_u16(p + 12, w);
    put_u16(p + 14, h);
    p[16] = depth;
    p[17] = npal;
    p[18] = esc;
}

int el01_write(FILE *out, uint32_t w, uint32_t h,
               const uint8_t *rgba, int with_mask)
{
    size_t n = (size_t)w * h;

    uint32_t *surface = xmalloc(n * sizeof(uint32_t));
    for (size_t y = 0; y < h; y++)
        for (size_t x = 0; x < w; x++) {
            const uint8_t *s = rgba + ((size_t)y * w + x) * 4;
            surface[(h - 1 - y) * w + x] =
                (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2];
        }

    int has_mask = with_mask;

    int gray = is_gray(surface, n);

    Out body = { 0 };
    uint8_t flags;
    uint8_t esc = pick_esc(surface, n, gray);
    if (gray) {
        flags = EL_ENC_GRAY;
        emit_gray_rle(&body, surface, n, esc);
    } else {
        flags = EL_ENC_RLE;
        emit_rle(&body, surface, n, esc);
    }

    Out mask = { 0 };
    if (has_mask) {
        size_t i = 0;
        while (i < n) {
            uint8_t v = rgba[i * 4 + 3];
            size_t run = 1;
            while (i + run < n && rgba[(i + run) * 4 + 3] == v)
                run++;
            while (run) {
                size_t take = run > 255 ? 255 : run;
                out_byte(&mask, v);
                out_byte(&mask, (uint8_t)take);
                i += take;
                run -= take;
            }
        }
    }

    uint16_t n_streams = has_mask ? 2 : 1;
    uint16_t extent = 16 + 4u * n_streams;
    uint32_t start1 = extent + 20u + (uint32_t)body.len;
    uint8_t outer[24];
    memset(outer, 0, sizeof outer);
    memcpy(outer, "EL01", 4);
    put_u16(outer + 4, 0x0100);
    put_u16(outer + 8, n_streams);
    put_u16(outer + 10, 16);
    put_u16(outer + 12, extent);
    put_u32(outer + 16, 0);
    if (n_streams == 2)
        put_u32(outer + 20, start1 - extent);

    fwrite(outer, 1, extent, out);

    uint8_t hdr[20];
    uint8_t color_flags = (uint8_t)(flags | (has_mask ? EL_FLAG_HAS_ALPHA : 0));
    write_stream_header(hdr, color_flags, start1 - extent, w, h, 24, 0, esc);
    fwrite(hdr, 1, 20, out);
    fwrite(body.buf, 1, body.len, out);

    if (n_streams == 2) {
        uint32_t tail = 20u + (uint32_t)mask.len;
        write_stream_header(hdr, EL_MASK_RLE, tail, w, h, 8, 0, 0);
        fwrite(hdr, 1, 20, out);
        fwrite(mask.buf, 1, mask.len, out);
    }

    free(surface);
    free(body.buf);
    free(mask.buf);
    return ferror(out) ? -1 : 0;
}
