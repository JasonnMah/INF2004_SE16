/* Actual Communication task-step code and UART adapter; kernel IPC/flash/radio
 * are modeled. Exercises button dispatch, body loans and automatic recovery. */
#include "app.h"
#include "upload.h"
#include "pc_config.h"
#include "batch_gateway.h"
#include "batch/batch_sensor.h"
#include "protocol/log_digest.h"
#include "protocol/crc16.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t flash[BATCH_STORE_BYTES],source[8448],pc[8448],rx[4096];
static unsigned head,tail,network=1,loan,posts,complete,failed,cleared,config_token;
static unsigned received,preamble_count,rx_fault,fail_upload_once,config_count;
static int upload_result;
static upload_job_t job;
static app_message_t button;
static uint64_t now;
static batch_sensor_t sensor;
unsigned long long rt_now_ms(void) { return now; }
void rt_sleep(unsigned ms) { now+=ms; }
void rt_log(const char *text) { (void)text; }
void rt_numbers(const char *format,unsigned a,unsigned b,unsigned c)
{ (void)format;(void)a;(void)b;(void)c; }
void rt_trace(const char *name,unsigned loops,unsigned unit) { (void)name;(void)loops;(void)unit; }
void rt_fatal(const char *what,int error) { fprintf(stderr,"FATAL %s %d\n",what,error);abort(); }
int rt_uart_open(unsigned unit) { assert(unit==0);return 0; }
void rt_wait(unsigned unit,unsigned timeout) { (void)unit;now+=timeout; }
int rt_status_send(const app_message_t *m)
{ complete+=m->kind==APP_STATUS_UPLOAD;failed+=m->kind==APP_STATUS_FAILED;return 1; }
int rt_command_take(app_message_t *m)
{ if (!button.kind)return 0;*m=button;button.kind=0;return 1; }
void rt_uart_write(unsigned unit,const unsigned char *bytes,unsigned length)
{
    assert(unit==0 && length>32);for(unsigned i=0;i<32;++i)assert(bytes[i]==0x55);
    ++preamble_count;protocol_frame_t f;assert(frame_decode(bytes+32,length-32,&f));
    batch_sensor_handle(&sensor,&f);
}
unsigned rt_rx_take(unsigned unit,unsigned char *bytes,unsigned capacity,unsigned *fault)
{
    assert(unit==0);*fault=rx_fault;rx_fault=0;unsigned n=tail-head;if(n>capacity)n=capacity;
    memcpy(bytes,rx+head,n);head+=n;if(head==tail)head=tail=0;return n;
}
static void sensor_write(void *ctx,const uint8_t *bytes,size_t length)
{ (void)ctx;assert(tail+length<sizeof(rx));memcpy(rx+tail,bytes,length);tail+=(unsigned)length; }
static uint64_t clock_now(void *ctx) { (void)ctx;return now; }
static bool erase(void *ctx,uint32_t offset)
{ (void)ctx;memset(flash+offset,0xff,4096);return true; }
static bool program(void *ctx,uint32_t offset,const uint8_t *bytes)
{ (void)ctx;for(unsigned i=0;i<256;++i){assert((flash[offset+i]&bytes[i])==bytes[i]);flash[offset+i]&=bytes[i];}return true; }
bool flash_port_init(batch_flash_t *port)
{ *port=(batch_flash_t){.mapped=flash,.erase=erase,.program=program};return true; }
unsigned upload_network_ready(void) { return network; }
int rt_upload_send(const upload_job_t *value)
{ assert(!loan);job=*value;loan=1;++posts;return 1; }
int rt_upload_result_take(void) { int r=upload_result;upload_result=0;return r; }
int rt_config_request_send(unsigned token) { config_token=token;return 1; }
int rt_config_result_take(pc_config_result_t *r)
{
    if (!config_token)return 0;
    *r=(pc_config_result_t){.token=config_token,.result=1,.interval_minutes=1,.unix_seconds=1700000000};
    config_token=0;return 1;
}
static bool configure(void *ctx,uint32_t timestamp,uint32_t seconds)
{ (void)ctx;assert(timestamp==1700000000 && seconds==60);++config_count;return true; }
static pico1_storage_result_t clear(void *ctx) { (void)ctx;++cleared;return P1_STORAGE_READY; }
static bool open_range(void *ctx,const batch_info_t *req,batch_info_t *info,uint8_t *bytes)
{
    (void)ctx;uint8_t hash[32];log_digest(source,sizeof(source),hash);
    if (!batch_digest_zero(req->sha256) && memcmp(req->sha256,hash,32))return false;
    *info=*req;memcpy(info->sha256,hash,32);info->total=sizeof(source);
    unsigned remaining=info->total-info->offset;info->length=remaining<4096?remaining:4096;
    memcpy(bytes,source+info->offset,info->length);info->crc=crc16_x25(bytes,info->length);return true;
}
static bool finish(void *ctx,const uint8_t *hash,uint32_t total)
{ (void)ctx;uint8_t expected[32];log_digest(source,sizeof(source),expected);return total==sizeof(source)&&!memcmp(hash,expected,32); }
static void press(unsigned pin) { button=(app_message_t){.kind=pin,.at_ms=now};communication_step(); }
static void step(bool deliver)
{
    now+=10;communication_step();batch_sensor_tick(&sensor);
    if (loan && deliver) {
        unsigned offset=0;for(unsigned i=71;i<79;++i){char c=job.id[i];offset=offset*16+(unsigned)(c<='9'?c-'0':c-'a'+10);}
        assert(job.total_length==sizeof(source) && !memcmp(job.data,source+offset,job.length));
        if (fail_upload_once) { fail_upload_once=0;upload_result=-1; }
        else { memcpy(pc+offset,job.data,job.length);received+=job.length;upload_result=1; }
        loan=0;
    }
}
int main(void)
{
    memset(flash,0xff,sizeof(flash));for(unsigned i=0;i<sizeof(source);++i)source[i]=(uint8_t)i;
    static const pico1_backend_t storage={.configure=configure,.clear_begin=clear};
    assert(batch_sensor_init(&sensor,(protocol_link_t){.session_id=0x11223344,.write=sensor_write,.now_ms=clock_now},
        &storage,(batch_source_t){.open=open_range,.finish=finish}));
    communication_init();press(APP_CONFIG);
    for(unsigned i=0;i<100;++i) { step(true); } assert(config_count==1);
    now+=1000;press(APP_RETRIEVE);
    for(unsigned i=0;i<5000 && !loan;++i) { step(false); } assert(loan && posts==1);
    uint8_t copy[4096];memcpy(copy,job.data,job.length);
    press(APP_CLEAR);press(APP_CONFIG);press(APP_RETRIEVE);
    rx_fault=1;for(unsigned i=0;i<100;++i)step(false);
    assert(!cleared && config_count==1 && posts==1 && !memcmp(job.data,copy,job.length));
    /* Reset while radio had borrowed the body. Real reset kills that task;
     * the persisted READY batch is then uploaded without another button. */
    loan=0;head=tail=0;communication_init();fail_upload_once=1;
    for(unsigned i=0;i<30000 && !complete;++i)step(true);
    assert(complete==1 && received==sizeof(source) && posts==5 && failed==1);
    assert(!memcmp(pc,source,sizeof(source)) && preamble_count>1024);
    assert(communication_snapshot().state==BG_IDLE);
    now+=2000;press(APP_CLEAR);for(unsigned i=0;i<100;++i) { step(true); } assert(cleared==1);
    puts("PASS actual batch Communication/UART adapter: PC CONFIG, one GP21, preambles, busy buttons, immutable IPC loan, restart auto-resume, retry, explicit CLEAR");
    return 0;
}
