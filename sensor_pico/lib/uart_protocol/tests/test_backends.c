/* Shared endpoints with actual page encoding and an asynchronous storage model. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pico1/pico1_protocol.h"
#include "pico2/pico2_protocol.h"
#include "protocol/stream_parser.h"
#include "protocol/storage_profile.h"
static uint8_t encoded_fixture[512];
static uint64_t now;
static pico1_ctx_t p1;
static pico2_ctx_t p2;
static stream_parser_t parser[2];
static unsigned char wire[2][8192], bytes[512], expected[512], received[1024];
static unsigned head[2],tail[2], sides[2]={0,1};
static unsigned completions,leases,releases,clears,clear_acks,end_acks;
static bool drop_end,drop_clear,clear_failed,clear_fail_at_finish,drop_data_all,cleared,oversized;
static uint64_t finish;
static uint64_t clock_ms(void *context) { (void)context; return now; }
static void write_bytes(void *context,const uint8_t *data,size_t length)
{
    unsigned side=*(unsigned *)context;
    protocol_frame_t f; assert(frame_decode(data,length,&f));
    if(side==0 && f.message_type==MSG_ACK && p1.transfer_state==P1_WAIT_DATA_ACK && drop_data_all) return;
    if(side==0 && f.message_type==MSG_ACK && p1.transfer_state==P1_WAIT_END_ACK) {
        ++end_acks; if(drop_end) { drop_end=false; return; }
    }
    if(side==1 && f.message_type==MSG_ACK && f.payload_length==2 &&
       p1.last_command_type==MSG_CLEAR_LOG) {
        ++clear_acks; assert(cleared); if(drop_clear) { drop_clear=false; return; }
    }
    for(size_t i=0;i<length;++i) { assert(head[1-side]-tail[1-side]<sizeof(wire[0])); wire[1-side][head[1-side]++%8192]=data[i]; }
}
static bool configure(void *context,uint32_t timestamp,uint32_t seconds)
{ (void)context;(void)timestamp;return seconds!=0 && seconds%60==0 && seconds/60<=65535; }
static bool snapshot(void *context,const uint8_t **data,size_t *length)
{
    (void)context;
    if(oversized) return false;
    *data=bytes; *length=cleared?0:sizeof(bytes); ++leases; return true;
}
static void release(void *context,bool completed)
{ (void)context;(void)completed; ++releases; }
static pico1_storage_result_t begin(void *context)
{
    (void)context;
    if(clear_failed) return P1_STORAGE_ERROR;
    ++clears; finish=now+5000; return P1_STORAGE_PENDING;
}
static pico1_storage_result_t poll_clear(void *context)
{
    (void)context;
    if(now<finish) return P1_STORAGE_PENDING;
    if(clear_fail_at_finish) { clear_failed=true; return P1_STORAGE_ERROR; }
    cleared=true; return P1_STORAGE_READY;
}
static void complete(pico2_ctx_t *p) { ++completions; assert(p->received_log_valid); }
static const pico1_backend_t backend={.configure=configure,.snapshot=snapshot,.clear_begin=begin,.clear_poll=poll_clear,.release=release};
static void pump(unsigned milliseconds)
{
    for(unsigned n=0;n<milliseconds;n+=10) {
        for(unsigned side=0;side<2;++side) while(tail[side]!=head[side]) {
            if(stream_parser_feed(&parser[side],wire[side][tail[side]++%8192])) {
                protocol_frame_t f; assert(frame_decode(parser[side].buffer,parser[side].length,&f));
                if(side==0) pico2_handle_frame(&p2,&f); else pico1_handle_frame(&p1,&f);
            }
        }
        pico1_tick(&p1); pico2_tick(&p2); now+=10;
    }
}
static void init(void)
{
    now=0; memset(head,0,sizeof(head));memset(tail,0,sizeof(tail));
    completions=leases=releases=clears=clear_acks=end_acks=0;
    drop_end=drop_clear=clear_failed=clear_fail_at_finish=drop_data_all=cleared=oversized=false;
    for(unsigned i=0;i<2;++i)stream_parser_init(&parser[i]);
    memcpy(bytes,encoded_fixture,sizeof(bytes));
    memcpy(expected,bytes,sizeof(bytes));
    protocol_link_t links[2];
    for(unsigned i=0;i<2;++i) links[i]=(protocol_link_t){.context=&sides[i],.session_id=0x11223344,.write=write_bytes,.now_ms=clock_ms};
    assert(pico1_init(&p1,links[1],NULL,0,(pico1_hooks_t){.backend=&backend}));
    assert(pico2_init(&p2,links[0],received,sizeof(received),1700000000,3600,(pico2_hooks_t){.transfer_complete=complete}));
    p2.state=P2_COMPLETE;
    assert(pico2_send_config(&p2));pump(100);assert(p1.configured && p2.state==P2_COMPLETE);
}
int main(int argc, char **argv)
{
    assert(argc==2);
    FILE *input=fopen(argv[1],"rb");assert(input);
    assert(fread(encoded_fixture,1,sizeof(encoded_fixture),input)==sizeof(encoded_fixture));fclose(input);
    init(); drop_end=true; assert(pico2_request_data(&p2));pump(2000);
    assert(p2.received_log_valid && p2.received_log_length==sizeof(bytes));
    assert(!memcmp(received,expected,sizeof(bytes)) && completions==1 && leases==1 && releases==1 && end_acks==2);
    init(); assert(pico2_request_data(&p2));pump(20);assert(leases==1 && releases==0);
    pico1_abort_transfer(&p1);assert(releases==1 && !p1.pending_transfer.active);
    init();drop_data_all=true;assert(pico2_request_data(&p2));pump(3000);
    assert(releases==1 && !p1.pending_transfer.active && p2.state==P2_FAILED && !p2.received_log_valid && !completions);
    init();p2.config_interval=1;assert(pico2_send_config(&p2));pump(500);
    assert(p2.state==P2_FAILED && p1.sampling_interval==3600);
    init();oversized=true;assert(pico2_request_data(&p2));pump(500);
    assert(p2.state==P2_FAILED && !p2.received_log_valid && !completions && !leases);
    init();assert(pico2_clear_log(&p2));pump(2500);
    assert(p2.state==P2_FAILED && !clear_acks && clears==1 && p1.clear_pending); /* Exposes old timing gap. */
    pump(3000);assert(cleared && clears==1 && clear_acks==1);
    init();p2.clear_ack_timeout_ms=STORAGE_CLEAR_ACK_TIMEOUT_MS;drop_clear=true;assert(pico2_clear_log(&p2));pump(2500);
    assert(p2.pending_command.active && !clear_acks && clears==1);
    pump(STORAGE_CLEAR_ACK_TIMEOUT_MS);assert(p2.state==P2_COMPLETE && cleared && clears==1 && clear_acks==2);
    assert(p2.pending_command.retries==1); /* Lost delayed ACK repeats ACK, never erase. */
    init();clear_failed=true;p2.clear_ack_timeout_ms=STORAGE_CLEAR_ACK_TIMEOUT_MS;assert(pico2_clear_log(&p2));pump(500);
    assert(p2.state==P2_FAILED && !cleared && !clear_acks);
    init();clear_fail_at_finish=true;p2.clear_ack_timeout_ms=STORAGE_CLEAR_ACK_TIMEOUT_MS;
    assert(pico2_clear_log(&p2));pump(5500);
    assert(p2.state==P2_FAILED && !cleared && !clear_acks && clears==1);
    puts("PASS: backend validation/leases/retry exhaustion, delayed verified CLEAR, lost delayed ACK, failed CLEAR");
}
