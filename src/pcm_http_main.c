#include "pcm_http.h"
#include "pcm_http_ubus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static uint64_t now_ms(void*c){(void)c;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(uint64_t)t.tv_sec*1000U+t.tv_nsec/1000000U;}
#ifdef DF_PCM_HTTP_TEST_PROGRAM
static int status(struct df_pcm_http_status*s,void*c){(void)c;strcpy(s->runtime_id,"0123456789abcdef");strcpy(s->call_state,"connected");s->generation=7;s->audio_tx_active=true;s->audio_tx_generation=7;return 0;} static int rnd(unsigned char*b,size_t n,void*c){(void)c;memset(b,0xab,n);return 0;}
#else
static int status(struct df_pcm_http_status*s,void*c){return df_pcm_http_ubus_status(s,c);} static int rnd(unsigned char*b,size_t n,void*c){FILE*f=fopen("/dev/urandom","rb");if(!f)return-1;size_t r=fread(b,1,n,f);fclose(f);return r==n?0:-1;}
#endif
static void out(struct df_pcm_http_response*r){printf("Status: %u\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n\r\n",r->http_status);if(r->http_status==200)printf("{\"runtime_id\":\"%s\",\"generation\":%llu,\"audio_session\":\"%s\",\"accepted_frames\":%llu,\"next_sequence\":%llu,\"lease_ms\":%llu}\n",r->runtime_id,(unsigned long long)r->generation,r->audio_session,(unsigned long long)r->accepted_frames,(unsigned long long)r->next_sequence,(unsigned long long)r->lease_ms);else printf("{\"error\":\"%s\"}\n",r->error);}
int main(void){const char*p=getenv("PATH_INFO");enum df_pcm_http_operation op;if(!p)return 0;if(!strcmp(p,"/api/v1/audio/session"))op=DF_PCM_HTTP_SESSION_OPEN;else if(!strcmp(p,"/api/v1/audio/submit.pcm"))op=DF_PCM_HTTP_SUBMIT;else if(!strcmp(p,"/api/v1/audio/session/end"))op=DF_PCM_HTTP_SESSION_END;else{struct df_pcm_http_response r={.http_status=404};strcpy(r.error,"not_found");out(&r);return 0;}char *q=getenv("QUERY_STRING"),*rid=NULL,*gen=NULL,*seq=NULL;if(q){char*s=strdup(q);for(char*t=strtok(s,"&");t;t=strtok(NULL,"&")){char*k=strchr(t,'=');if(!k){free(s);goto bad;}*k++=0;if(!strcmp(t,"runtime_id")&&!rid)rid=k;else if(!strcmp(t,"generation")&&!gen)gen=k;else if(!strcmp(t,"sequence")&&!seq)seq=k;else{free(s);goto bad;}}}struct df_pcm_http_request req={op,getenv("REQUEST_METHOD"),rid,gen,seq,getenv("HTTP_X_DOORFAST_AUDIO_SESSION"),getenv("CONTENT_TYPE"),getenv("CONTENT_LENGTH"),0};struct df_pcm_http_config cfg={"/tmp/doorfast-pcm-http.state","/var/run/doorfast-audio.sock",{status,rnd,now_ms,NULL,df_pcm_http_ubus_send,NULL}};struct df_pcm_http_response r;df_pcm_http_handle(&cfg,&req,&r);out(&r);return 0;bad:{struct df_pcm_http_response r={.http_status=400};strcpy(r.error,"invalid_request");out(&r);return 0;}}
