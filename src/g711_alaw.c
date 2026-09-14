#include "g711_alaw.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int16_t df_g711_alaw_decode_sample(uint8_t value){int sample,segment;value^=0x55;sample=(value&0x0f)<<4;segment=(value&0x70)>>4;switch(segment){case 0:sample+=8;break;case 1:sample+=0x108;break;default:sample+=0x108;sample<<=segment-1;}return (int16_t)((value&0x80)?sample:-sample);}
int df_g711_alaw_decode(const uint8_t *input,size_t length,int16_t *output,size_t capacity){size_t i;if(input==NULL||output==NULL||capacity<length)return -1;for(i=0;i<length;i++)output[i]=df_g711_alaw_decode_sample(input[i]);return 0;}
static void le16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void le32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
int df_g711_alaw_write_wav(const char *path,const uint8_t *input,size_t length){char temporary[512];uint8_t header[44]={0};FILE *file;size_t i;if(path==NULL||input==NULL||length==0||length>0x7fffffd0U||snprintf(temporary,sizeof(temporary),"%s.tmp.%ld",path,(long)getpid())<0)return -1;memcpy(header,"RIFF",4);le32(header+4,(uint32_t)(36+length*2));memcpy(header+8,"WAVEfmt ",8);le32(header+16,16);le16(header+20,1);le16(header+22,1);le32(header+24,8000);le32(header+28,16000);le16(header+32,2);le16(header+34,16);memcpy(header+36,"data",4);le32(header+40,(uint32_t)(length*2));file=fopen(temporary,"wb");if(file==NULL)return -1;if(fwrite(header,1,sizeof(header),file)!=sizeof(header)){fclose(file);unlink(temporary);return -1;}for(i=0;i<length;i++){uint8_t pcm[2];le16(pcm,(uint16_t)df_g711_alaw_decode_sample(input[i]));if(fwrite(pcm,1,2,file)!=2){fclose(file);unlink(temporary);return -1;}}if(fflush(file)!=0||fclose(file)!=0||rename(temporary,path)!=0){unlink(temporary);return -1;}return 0;}
