#include "el01.h"
#include "imgin.h"
#include "imgout.h"

#include <errno.h>
#include <string.h>

#include "common.h"

static const char *stem_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    static char stem[512];
    size_t n = strcspn(base, ".");
    if (n >= sizeof stem)
        n = sizeof stem - 1;
    memcpy(stem, base, n);
    stem[n] = '\0';
    return stem;
}

static int cmd_decode(const char *in_path, const char *out_path)
{
    size_t len;
    FILE *f = fopen(in_path, "rb");
    if (!f)
        die("cannot open %s: %s", in_path, strerror(errno));
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    uint8_t *data = xmalloc((size_t)sz);
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz)
        die("short read on %s", in_path);
    fclose(f);
    len = (size_t)sz;

    ElImageRaw img;
    if (el01_decode_raw(data, len, &img) != 0)
        die("%s is not a decodable EL01 file", in_path);
    uint8_t *rgba = el01_compose(&img);
    f = fopen(out_path, "wb");
    if (!f)
        die("cannot create %s", out_path);
    if (png_write(f, img.w, img.h, rgba, img.mask != NULL) != 0 ||
        fclose(f) != 0)
        die("write failed on %s", out_path);
    free(rgba);
    el01_raw_free(&img);
    free(data);
    return 0;
}

static int cmd_encode(const char *in_path, const char *out_path)
{
    uint32_t w, h;
    int has_alpha;
    uint8_t *rgba = image_read(in_path, &w, &h, &has_alpha);
    FILE *f = fopen(out_path, "wb");
    if (!f)
        die("cannot create %s", out_path);
    if (el01_write(f, w, h, rgba, has_alpha) != 0 || fclose(f) != 0)
        die("write failed on %s", out_path);
    free(rgba);
    printf("%s: wrote %ux%u EL01 image\n", out_path, w, h);
    return 0;
}

static void usage(void)
{
    fputs("usage:\n"
          "  el01 decode IMAGE.el [-o OUT.png]\n"
          "  el01 encode IMAGE.pam|IMAGE.bmp [-o OUT.el]\n",
          stderr);
    exit(2);
}

int main(int argc, char **argv)
{
    if (argc < 3)
        usage();
    const char *cmd = argv[1];
    int is_encode = strcmp(cmd, "encode") == 0;

    if (strcmp(cmd, is_encode ? "encode" : "decode") != 0)
        usage();

    const char *in = argv[2];
    const char *out = NULL;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            out = argv[++i];
        else
            usage();
    }
    char default_out[600];
    if (!out) {
        snprintf(default_out, sizeof default_out, "%s.%s", stem_of(in),
                 is_encode ? "el" : "png");
        out = default_out;
    }
    return is_encode ? cmd_encode(in, out) : cmd_decode(in, out);
}
