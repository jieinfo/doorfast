#include "test.h"
#include "gvs_video_snapshot.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
void test_gvs_video_snapshot(void){char path[128];unsigned char got[4];const unsigned char frame[]={0xff,0xd8,0xff,0xd9};FILE *f;(void)snprintf(path,sizeof(path),"/tmp/doorfast-snapshot-%ld.jpg",(long)getpid());TEST_ASSERT_INT_EQ(0,df_gvs_video_snapshot_write(path,frame,sizeof(frame)));f=fopen(path,"rb");TEST_ASSERT_INT_EQ(1,f!=NULL);if(f!=NULL){TEST_ASSERT_INT_EQ(4,(int)fread(got,1,sizeof(got),f));(void)fclose(f);TEST_ASSERT_INT_EQ(0,memcmp(got,frame,4));}(void)unlink(path);}
