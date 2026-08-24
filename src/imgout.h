#ifndef ELDUMP_IMGOUT_H
#define ELDUMP_IMGOUT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int png_write(FILE *f, uint32_t w, uint32_t h, const uint8_t *rgba_topdown,
              int has_alpha);

#endif
