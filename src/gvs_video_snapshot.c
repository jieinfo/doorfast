#include "gvs_video_snapshot.h"
#include <stdio.h>
#include <unistd.h>
int df_gvs_video_snapshot_write(const char *path,const uint8_t *data,size_t length){char temporary[512];FILE *file;if(path==NULL||data==NULL||length==0||snprintf(temporary,sizeof(temporary),"%s.tmp.%ld",path,(long)getpid())<0)return -1;file=fopen(temporary,"wb");if(file==NULL)return -1;if(fwrite(data,1,length,file)!=length||fflush(file)!=0||fclose(file)!=0){(void)unlink(temporary);return -1;}if(rename(temporary,path)!=0){(void)unlink(temporary);return -1;}return 0;}
