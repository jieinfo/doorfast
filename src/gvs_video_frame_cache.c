#include "gvs_video_frame_cache.h"
#include <stdlib.h>
#include <string.h>
void df_gvs_video_frame_cache_init(struct df_gvs_video_frame_cache *c){if(c!=NULL)memset(c,0,sizeof(*c));}
void df_gvs_video_frame_cache_reset(struct df_gvs_video_frame_cache *c){if(c==NULL)return;free(c->data);memset(c,0,sizeof(*c));}
int df_gvs_video_frame_cache_store(struct df_gvs_video_frame_cache *c,const uint8_t *data,size_t length,uint64_t generation,uint64_t timestamp_ms){uint8_t *next;if(c==NULL||data==NULL||length==0||length>DF_GVS_VIDEO_CACHE_MAX)return -1;if(c->capacity<length){next=realloc(c->data,length);if(next==NULL)return -1;c->data=next;c->capacity=length;}memcpy(c->data,data,length);c->length=length;c->generation=generation;c->timestamp_ms=timestamp_ms;c->valid=true;return 0;}
int df_gvs_video_frame_cache_snapshot(const struct df_gvs_video_frame_cache *c,const uint8_t **data,size_t *length,uint64_t *generation,uint64_t *timestamp_ms){if(c==NULL||data==NULL||length==NULL||generation==NULL||timestamp_ms==NULL||!c->valid)return -1;*data=c->data;*length=c->length;*generation=c->generation;*timestamp_ms=c->timestamp_ms;return 0;}
