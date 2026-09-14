#include "gvs_media.h"
#include <string.h>
static uint16_t le16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t le32(const uint8_t *p){return (uint32_t)(p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24));}
static void put_le16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put_le32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static int common(const uint8_t *p,size_t n){static const uint8_t m[10]={0x47,0x56,0x53,0x47,0x56,0x53,0xa5,0xa5,0xa5,0xa5};return p!=NULL&&n>=16&&memcmp(p,m,10)==0;}
int df_gvs_parse_audio(const uint8_t *p,size_t n,struct df_gvs_audio_packet *o){size_t len;if(!common(p,n)||o==NULL||n<DF_GVS_AUDIO_HEADER_LEN)return -1;len=le16(p+0x22);if(len>n-DF_GVS_AUDIO_HEADER_LEN)return -1;memcpy(o->destination,p+10,6);memcpy(o->source,p+16,6);o->sequence=le16(p+0x18);o->field_c=le32(p+0x1a);o->field_d=le16(p+0x1e);o->field_e=le16(p+0x20);o->field_f=le16(p+0x24);o->payload=p+DF_GVS_AUDIO_HEADER_LEN;o->payload_length=len;return 0;}
int df_gvs_parse_video(const uint8_t *p,size_t n,struct df_gvs_video_packet *o){size_t len;if(!common(p,n)||o==NULL||n<DF_GVS_VIDEO_HEADER_LEN)return -1;len=le16(p+0x22);if(len==0||len>1200||len>n-DF_GVS_VIDEO_HEADER_LEN||le32(p+0x1a)==0)return -1;o->chunk_count=le16(p+0x1e);o->chunk_index=le16(p+0x20);if(o->chunk_count==0||o->chunk_index==0||o->chunk_index>o->chunk_count)return -1;memcpy(o->destination,p+10,6);memcpy(o->source,p+16,6);o->frame_no=le16(p+0x18);o->full_length=le32(p+0x1a);o->chunk_length=(uint16_t)len;o->capacity=le16(p+0x24);o->payload=p+DF_GVS_VIDEO_HEADER_LEN;return 0;}
int df_gvs_serialize_audio(const uint8_t destination[6],
                           const uint8_t source[6], uint16_t sequence,
                           const uint8_t *payload, size_t payload_length,
                           uint8_t *output, size_t capacity,
                           size_t *output_length)
{
    static const uint8_t magic[10] = {
        0x47, 0x56, 0x53, 0x47, 0x56, 0x53, 0xa5, 0xa5, 0xa5, 0xa5
    };
    size_t total = DF_GVS_AUDIO_HEADER_LEN + payload_length;

    if (destination == NULL || source == NULL || payload == NULL ||
        payload_length == 0 || payload_length > DF_GVS_AUDIO_MAX_PAYLOAD ||
        output == NULL || output_length == NULL || capacity < total) {
        return -1;
    }
    memset(output, 0, total);
    memcpy(output, magic, sizeof(magic));
    memcpy(output + 10, destination, 6);
    memcpy(output + 16, source, 6);
    put_le16(output + 0x18, sequence);
    put_le32(output + 0x1a, (uint32_t)payload_length);
    put_le16(output + 0x1e, 1);
    put_le16(output + 0x20, 1);
    put_le16(output + 0x22, (uint16_t)payload_length);
    put_le16(output + 0x24, 0x100);
    memcpy(output + DF_GVS_AUDIO_HEADER_LEN, payload, payload_length);
    *output_length = total;
    return 0;
}
