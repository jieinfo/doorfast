#include "pcm_http_ubus.h"
#include "gvs_pcm_ingress.h"
#ifdef DF_WITH_UBUS
#include <libubus.h>
#endif
int df_pcm_http_ubus_status(struct df_pcm_http_status *s, void *ctx){(void)s;(void)ctx; return -1;}
int df_pcm_http_ubus_send(const char *p,uint64_t g,const int16_t *x,size_t n,void *c){(void)c; return df_gvs_pcm_ingress_send(p,g,x,n)==DF_OK?DF_OK:DF_ERR_IO;}
