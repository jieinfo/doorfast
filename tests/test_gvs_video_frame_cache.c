#include "test.h"
#include "gvs_video_frame_cache.h"
#include <string.h>
void test_gvs_video_frame_cache(void){struct df_gvs_video_frame_cache c;const uint8_t *data;size_t length;uint64_t generation,time;const uint8_t frame[]={0xff,0xd8,0xff,0xd9};df_gvs_video_frame_cache_init(&c);TEST_ASSERT_INT_EQ(-1,df_gvs_video_frame_cache_snapshot(&c,&data,&length,&generation,&time));TEST_ASSERT_INT_EQ(0,df_gvs_video_frame_cache_store(&c,frame,sizeof(frame),9,123));TEST_ASSERT_INT_EQ(0,df_gvs_video_frame_cache_snapshot(&c,&data,&length,&generation,&time));TEST_ASSERT_INT_EQ(4,(int)length);TEST_ASSERT_INT_EQ(0,memcmp(data,frame,4));TEST_ASSERT_INT_EQ(9,(int)generation);TEST_ASSERT_INT_EQ(123,(int)time);df_gvs_video_frame_cache_reset(&c);}
