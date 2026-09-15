#include "pcm_http_ubus.h"
#include "gvs_pcm_ingress.h"
#include "runtime_id.h"
#include <stdio.h>
#ifdef DF_WITH_UBUS
#include <libubus.h>
#include <libubox/blobmsg_json.h>
enum { RID, CALL, AUDIO, ROOT_N };
static const struct blobmsg_policy rp[ROOT_N]={{"runtime_id",BLOBMSG_TYPE_STRING},{"call",BLOBMSG_TYPE_TABLE},{"audio_tx",BLOBMSG_TYPE_TABLE}};
enum { SESSION, GEN, CALL_N }; static const struct blobmsg_policy cp[CALL_N]={{"session",BLOBMSG_TYPE_STRING},{"generation",BLOBMSG_TYPE_INT64}};
enum { ACTIVE, TXGEN, AUDIO_N }; static const struct blobmsg_policy ap[AUDIO_N]={{"active",BLOBMSG_TYPE_BOOL},{"generation",BLOBMSG_TYPE_INT64}};
struct pc { struct df_pcm_http_status *s; int ok; };
static void cb(struct ubus_request *r,int type,struct blob_attr *m){(void)type;struct pc*c=r->priv;struct blob_attr*x[ROOT_N],*y[CALL_N],*z[AUDIO_N];blobmsg_parse(rp,ROOT_N,x,blobmsg_data(m),blobmsg_len(m));if(!x[RID]||!x[CALL]||!x[AUDIO])return;blobmsg_parse(cp,CALL_N,y,blobmsg_data(x[CALL]),blobmsg_len(x[CALL]));blobmsg_parse(ap,AUDIO_N,z,blobmsg_data(x[AUDIO]),blobmsg_len(x[AUDIO]));if(!y[SESSION]||!y[GEN]||!z[ACTIVE]||!z[TXGEN])return;const char*id=blobmsg_get_string(x[RID]);uint64_t g=blobmsg_get_u64(y[GEN]),ag=blobmsg_get_u64(z[TXGEN]);if(!df_runtime_id_is_valid(id)||!g||!ag||g!=ag)return;snprintf(c->s->runtime_id,sizeof(c->s->runtime_id),"%s",id);snprintf(c->s->call_state,sizeof(c->s->call_state),"%s",blobmsg_get_string(y[SESSION]));c->s->generation=g;c->s->audio_tx_generation=ag;c->s->audio_tx_active=blobmsg_get_bool(z[ACTIVE]);c->ok=1;}
#endif
int df_pcm_http_ubus_status(struct df_pcm_http_status*s,void*ctx){
#ifdef DF_WITH_UBUS
(void)ctx;if(!s)return -1;struct ubus_context*u=ubus_connect(NULL);if(!u)return -1;uint32_t id;if(ubus_lookup_id(u,"doorfast",&id)){ubus_free(u);return -1;}struct pc p={s,0};int rc=ubus_invoke(u,id,"status",NULL,cb,&p,1000);ubus_free(u);return rc||!p.ok?-1:0;
#else
(void)s;(void)ctx;return -1;
#endif
}
int df_pcm_http_ubus_send(const char*p,uint64_t g,const int16_t*x,size_t n,void*c){(void)c;return df_gvs_pcm_ingress_send(p,g,x,n)==DF_OK?DF_OK:DF_ERR_IO;}
