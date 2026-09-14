#ifndef DOORFAST_G711_ALAW_H
#define DOORFAST_G711_ALAW_H
#include <stddef.h>
#include <stdint.h>
int16_t df_g711_alaw_decode_sample(uint8_t);
int df_g711_alaw_decode(const uint8_t *, size_t, int16_t *, size_t);
int df_g711_alaw_write_wav(const char *, const uint8_t *, size_t);
#endif
