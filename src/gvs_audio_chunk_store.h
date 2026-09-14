#ifndef DOORFAST_GVS_AUDIO_CHUNK_STORE_H
#define DOORFAST_GVS_AUDIO_CHUNK_STORE_H

#include <stddef.h>
#include <stdint.h>

#define DF_GVS_AUDIO_CHUNK_LIMIT 4

int df_gvs_audio_chunk_store_publish(const char *, const char *, uint64_t,
    uint64_t, uint64_t, size_t, uint64_t);
int df_gvs_audio_chunk_store_clear(const char *);

#endif
