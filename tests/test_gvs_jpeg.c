#include "test.h"
#include "gvs_video_reassembly.h"
void test_gvs_jpeg(void){const unsigned char good[]={0xff,0xd8,0x01,0xff,0xd9};const unsigned char bad[]={0xff,0xd8,0x01,0x02,0xd9};TEST_ASSERT_INT_EQ(0,df_gvs_jpeg_validate(good,sizeof(good)));TEST_ASSERT_INT_EQ(-1,df_gvs_jpeg_validate(bad,sizeof(bad)));TEST_ASSERT_INT_EQ(-1,df_gvs_jpeg_validate(good,3));}
