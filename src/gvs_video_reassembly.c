#include "gvs_video_reassembly.h"
#include <stdlib.h>
#include <string.h>
int df_gvs_jpeg_validate(const uint8_t *data, size_t length) { if (data == NULL || length < 4) return -1; return data[0] == 0xff && data[1] == 0xd8 && data[length - 2] == 0xff && data[length - 1] == 0xd9 ? 0 : -1; }
void df_gvs_video_reassembly_init(struct df_gvs_video_reassembly *r){if(r!=NULL)memset(r,0,sizeof(*r));}
void df_gvs_video_reassembly_reset(struct df_gvs_video_reassembly *r){if(r==NULL)return;free(r->buffer);memset(r,0,sizeof(*r));}
int df_gvs_video_reassembly_push(struct df_gvs_video_reassembly *r,
    const struct df_gvs_video_packet *p, const uint8_t **out, size_t *out_len) {
 if (r == NULL || p == NULL || out == NULL || out_len == NULL) return -1;
 *out = NULL; *out_len = 0;
 if (p->chunk_index == 1) {
  df_gvs_video_reassembly_reset(r);
  if (p->full_length == 0 || p->full_length > DF_GVS_VIDEO_MAX_FRAME || p->chunk_count == 0) return -1;
  r->buffer = malloc(p->full_length); if (r->buffer == NULL) return -1;
  r->capacity = p->full_length; r->frame_no = p->frame_no;
  r->chunk_count = p->chunk_count; r->next_chunk = 1; r->full_length = p->full_length;
 }
 if (r->buffer == NULL || p->frame_no != r->frame_no || p->chunk_count != r->chunk_count || p->chunk_index != r->next_chunk || p->chunk_length > r->full_length - r->received) { df_gvs_video_reassembly_reset(r); return -1; }
 memcpy(r->buffer + r->received, p->payload, p->chunk_length); r->received += p->chunk_length; r->next_chunk++;
 if (p->chunk_index == r->chunk_count) { if (r->received != r->full_length) { df_gvs_video_reassembly_reset(r); return -1; } *out = r->buffer; *out_len = r->received; return 1; }
 return 0;
}
