#ifndef DOORFAST_GVS_AUDIO_CHUNK_STORE_H
#define DOORFAST_GVS_AUDIO_CHUNK_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DF_GVS_AUDIO_CHUNK_LIMIT 20

int df_gvs_audio_chunk_store_publish(const char *, const char *, uint64_t,
    uint64_t, uint64_t, size_t, uint64_t);
int df_gvs_audio_chunk_store_publish_v1(const char *directory,
    const char *source, uint64_t generation, uint64_t previous_revision,
    uint64_t revision, size_t bytes, uint64_t dropped_samples,
    uint64_t timestamp_ms, bool discontinuity);
int df_gvs_audio_chunk_store_clear(const char *);

#endif
