#include "test.h"
#include "gvs_media.h"
#include <string.h>
static void header(unsigned char *p){static const unsigned char m[10]={0x47,0x56,0x53,0x47,0x56,0x53,0xa5,0xa5,0xa5,0xa5};memcpy(p,m,10);}
void test_gvs_media(void){unsigned char a[0x2a+3]={0},v[0x26+3]={0};struct df_gvs_audio_packet ap;struct df_gvs_video_packet vp;header(a);a[0x22]=3;a[0x2a]=1;TEST_ASSERT_INT_EQ(0,df_gvs_parse_audio(a,sizeof(a),&ap));TEST_ASSERT_INT_EQ(3,(int)ap.payload_length);a[0x22]=4;TEST_ASSERT_INT_EQ(-1,df_gvs_parse_audio(a,sizeof(a),&ap));header(v);v[0x1a]=3;v[0x1e]=1;v[0x20]=1;v[0x22]=3;TEST_ASSERT_INT_EQ(0,df_gvs_parse_video(v,sizeof(v),&vp));v[0x20]=2;TEST_ASSERT_INT_EQ(-1,df_gvs_parse_video(v,sizeof(v),&vp));}
