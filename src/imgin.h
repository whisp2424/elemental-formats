#ifndef ELDUMP_IMGIN_H
#define ELDUMP_IMGIN_H

#include <stddef.h>
#include <stdint.h>

uint8_t *image_read(const char *path, uint32_t *w, uint32_t *h,
                    int *has_alpha);

#endif
