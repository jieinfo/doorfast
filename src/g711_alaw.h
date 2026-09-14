#ifndef DOORFAST_G711_ALAW_H
#define DOORFAST_G711_ALAW_H
#include <stddef.h>
#include <stdint.h>
int16_t df_g711_alaw_decode_sample(uint8_t);
uint8_t df_g711_alaw_encode_sample(int16_t);
int df_g711_alaw_encode(const int16_t *, size_t, uint8_t *, size_t);
int df_g711_alaw_decode(const uint8_t *, size_t, int16_t *, size_t);
int df_g711_alaw_write_wav(const char *, const uint8_t *, size_t);
#endif
