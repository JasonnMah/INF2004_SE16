/* Actual kernel_io setup/radio functions with a four-buffer host kernel model.
 * This checks copied-message routing and capacity, not physical SMP scheduling. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "upload.h"
#include "pc_config.h"
#include "rx_ring.h"
typedef int ID, ER, INT;
typedef unsigned UW;
typedef struct { unsigned flgatr, iflgptn; } T_CFLG;
typedef struct { unsigned mbfatr, bufsz, maxmsz; void *bufptr; } T_CMBF;
#define E_OK 0
#define E_TMOUT (-50)
#define E_LIMIT (-5)
#define TMO_POL 0
#define TA_TFIFO 1u
#define TA_USERBUF 2u
#define COMMAND_NOTIFY 2u
static unsigned flags_created, buffers_created, notifications;
static struct { T_CMBF definition; unsigned used; } buffers[4];
static ID tk_cre_flg(const T_CFLG *definition)
{ assert(definition->iflgptn==0);return (ID)++flags_created; }
static ID tk_cre_mbf(const T_CMBF *definition)
{
    if(buffers_created==4) return E_LIMIT;
    assert(definition->bufsz>=definition->maxmsz+sizeof(INT));
    buffers[buffers_created].definition=*definition;
    buffers[buffers_created].used=0;
    return (ID)++buffers_created;
}
static ER tk_set_flg(ID flag,unsigned pattern)
{ assert(flag>0&&flag<=2&&pattern==COMMAND_NOTIFY);++notifications;return E_OK; }
static ER tk_snd_mbf(ID id,const void *message,unsigned length,int timeout)
{
    assert(id>=1&&id<=4&&timeout==TMO_POL);
    unsigned i=(unsigned)id-1;
    T_CMBF *q=&buffers[i].definition;
    assert(length<=q->maxmsz);
    if(buffers[i].used+sizeof(INT)+length>q->bufsz) return E_TMOUT;
    unsigned char *end=(unsigned char *)q->bufptr+buffers[i].used;
    INT n=(INT)length;memcpy(end,&n,sizeof(n));memcpy(end+sizeof(n),message,length);
    buffers[i].used+=sizeof(n)+length;return E_OK;
}
static INT tk_rcv_mbf(ID id,void *destination,int timeout)
{
    assert(id>=1&&id<=4&&timeout==TMO_POL);
    unsigned i=(unsigned)id-1;
    if(!buffers[i].used) return E_TMOUT;
    unsigned char *start=buffers[i].definition.bufptr;
    INT n;memcpy(&n,start,sizeof(n));assert(n>0);
    memcpy(destination,start+sizeof(n),(unsigned)n);
    unsigned consumed=sizeof(n)+(unsigned)n;
    buffers[i].used-=consumed;
    memmove(start,start+consumed,buffers[i].used);return n;
}
void rt_fatal(const char *operation,int error)
{ fprintf(stderr,"Unexpected fatal: %s %d\n",operation,error);assert(0); }
/* Generated directly from the production source; no copied implementation. */
#include "radio_ipc_actual.inc"
int main(void)
{
    assert(rt_setup()==E_OK&&buffers_created==4&&flags_created==2);
    unsigned token;
    upload_job_t job;
    pc_config_result_t result;
    assert(!rt_config_request_take(&token)&&!rt_upload_take(&job));
    assert(rt_config_request_send(7));
    assert(!rt_upload_take(&job)); /* Peeking another kind must preserve it. */
    assert(rt_config_request_take(&token)&&token==7);
    static const unsigned char bytes[]={0,0x7d,0x7e,0xff};
    const upload_job_t original={.data=bytes,.length=sizeof(bytes),.id="log-ipc"
#if APP_BATCHED
        ,.total_length=1048576
#endif
    };
    assert(rt_upload_send(&original));
    assert(!rt_config_request_take(&token));
    assert(rt_upload_take(&job)&&job.data==bytes&&job.length==sizeof(bytes)&&!strcmp(job.id,"log-ipc"));
#if APP_BATCHED
    assert(job.total_length==1048576);
#endif
    pc_config_result_t config={.token=7,.result=1,.interval_minutes=4095,.unix_seconds=1793491200u};
    rt_config_result_send(&config);
    config.interval_minutes=2; /* Queue holds a copy, not the stack address. */
    assert(!rt_upload_result_take());
    assert(rt_config_result_take(&result)&&result.token==7&&result.interval_minutes==4095);
    assert(!rt_config_result_take(&result));
    rt_upload_result_send(-1);
    assert(!rt_config_result_take(&result));
    assert(rt_upload_result_take()==-1&&!rt_upload_result_take());
    assert(notifications==2);
    unsigned queued=0;
    while(rt_config_request_send(100+queued)) assert(++queued<10);
    assert(queued);
    for(unsigned n=0;n<queued;++n) assert(rt_config_request_take(&token)&&token==100+n);
    assert(!rt_config_request_take(&token)&&buffers_created==4);
    puts("PASS actual IPC: four-buffer setup, copied settings/results, upload/config routing, backpressure and notifications");
}
