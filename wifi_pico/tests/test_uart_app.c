/* Host model of task IPC and UART wire delivery, NOT a kernel/ISR emulator. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "rx_ring.h"
#include "ui_logic.h"
#include "protocol_io.h"
#include "pico2/pico2_protocol.h"
static rx_ring_t channels[2];
static unsigned long long now;
static app_message_t commands[APP_QUEUE_DEPTH], statuses[APP_QUEUE_DEPTH];
static unsigned ch, ct, cc, sh, st, sc;
static unsigned config_sends, end_sends, final_acks;
static uint16_t end_sequence;
static bool corrupt_first_data, corrupted;
unsigned long long rt_now_ms(void) { return now; }
void rt_sleep(unsigned ms) { now += ms; }
void rt_log(const char *s) { fputs(s, stdout); }
void rt_numbers(const char *s, unsigned a, unsigned b, unsigned c) { printf(s,a,b,c); }
void rt_trace(const char *n, unsigned a, unsigned b) { (void)n; (void)a; (void)b; }
void rt_fatal(const char *s, int e) { fprintf(stderr,"FATAL %s %d\n",s,e); abort(); }
int rt_setup(void) { return 0; }
int rt_uart_open(unsigned u) { (void)u; return 0; }
void rt_wait(unsigned u, unsigned t) { (void)u; now += t; }
void rt_buttons_init(void) {}
unsigned rt_button_low(unsigned p) { (void)p; return 0; }
void rt_led_init(void) {}
void rt_led_set(unsigned v) { (void)v; }
int rt_command_send(const app_message_t *m)
{
    if (cc == APP_QUEUE_DEPTH) { return 0; }
    commands[ch] = *m; ch=(ch+1)%APP_QUEUE_DEPTH; ++cc; return 1;
}
int rt_command_take(app_message_t *m)
{
    if (!cc) { return 0; }
    *m=commands[ct]; ct=(ct+1)%APP_QUEUE_DEPTH; --cc; return 1;
}
int rt_status_send(const app_message_t *m)
{
    if (sc==APP_QUEUE_DEPTH) { return 0; }
    statuses[sh]=*m; sh=(sh+1)%APP_QUEUE_DEPTH; ++sc; return 1;
}
int rt_status_take(app_message_t *m)
{
    if (!sc) { return 0; }
    *m=statuses[st]; st=(st+1)%APP_QUEUE_DEPTH; --sc; return 1;
}
unsigned rt_rx_take(unsigned u, unsigned char *out, unsigned capacity, unsigned *fault)
{ return rx_ring_take(&channels[u],out,capacity,fault); }
void rt_uart_write(unsigned u, const unsigned char *bytes, unsigned length)
{
    protocol_frame_t frame;
    assert(frame_decode(bytes,length,&frame));
    if (u==0 && frame.message_type==MSG_CONFIG) { ++config_sends; }
    if (u==1 && frame.message_type==MSG_TRANSFER_END) {
        ++end_sends; end_sequence=frame.sequence;
    }
    if (u==0 && frame.message_type==MSG_ACK && end_sends &&
        read_u16_le(frame.payload)==end_sequence) { ++final_acks; }
    unsigned char scratch[MAX_STUFFED_FRAME_SIZE];
    if (u==1 && frame.message_type==MSG_DATA && corrupt_first_data && !corrupted) {
        memcpy(scratch,bytes,length); scratch[1]^=0x10; bytes=scratch; corrupted=true;
    }
    for (unsigned i=0;i<length;++i) { rx_ring_push(&channels[1-u],bytes[i]); }
}
static void pump(unsigned ms)
{
    for (unsigned i=0;i<ms;i+=10) {
        communication_step(); sensor_emulator_step();
        /* Model the Status Task consuming messages; LED timing tested separately. */
        app_message_t m; while (rt_status_take(&m)) {}
        now += 10;
    }
}
static void press(unsigned pin)
{
    app_message_t m={.kind=pin,.at_ms=now}; assert(rt_command_send(&m));
}
static void init(void)
{
    now=0; ch=ct=cc=sh=st=sc=0;
    config_sends=end_sends=final_acks=0; end_sequence=0;
    corrupted=corrupt_first_data=false;
    rx_ring_init(&channels[0]); rx_ring_init(&channels[1]);
    fault_reset(); communication_init(); sensor_emulator_init();
}
static void configured(void)
{
    press(APP_CONFIG); pump(1100);
    assert(communication_snapshot().configured);
    assert(communication_snapshot().state==P2_COMPLETE);
}
static void valid_log(unsigned length,unsigned completions)
{
    communication_snapshot_t s=communication_snapshot();
    assert(s.state==P2_COMPLETE && s.valid && s.length==length && s.completions==completions);
}
static void primitives(void)
{
    rx_ring_t r; rx_ring_init(&r);
    unsigned char out[64]; unsigned fault;
    for(unsigned i=0;i<APP_RX_CAPACITY-1;++i) { rx_ring_push(&r,(unsigned char)i); }
    rx_ring_push(&r,0);
    assert(rx_ring_take(&r,out,sizeof(out),&fault)==0 && fault==RX_FAULT_OVERFLOW);
    assert(r.stats.dropped==1);
    for (unsigned round=0;round<100;++round) {
        for(unsigned i=0;i<64;++i) { rx_ring_push(&r,(unsigned char)i); }
        assert(rx_ring_take(&r,out,64,&fault)==64 && !fault);
        for(unsigned i=0;i<64;++i) { assert(out[i]==(unsigned char)i); }
    }
    rx_ring_fault(&r,RX_FAULT_HARDWARE); rx_ring_push(&r,0x7e);
    assert(rx_ring_take(&r,out,64,&fault)==0 && fault==RX_FAULT_HARDWARE);
    rx_ring_push(&r,42);
    assert(rx_ring_take(&r,out,64,&fault)==1 && !fault && out[0]==42);
    app_button_t b={0};
    assert(!app_button_event(&b,1,10));
    assert(!app_button_event(&b,1,30));
    assert(app_button_event(&b,1,40));
    assert(!app_button_event(&b,1,500)); /* Hold cannot retrigger. */
    assert(!app_button_event(&b,0,510));
    assert(!app_button_event(&b,1,520)); /* Release bounce cannot re-arm. */
    assert(!app_button_event(&b,0,600));
    assert(!app_button_event(&b,0,630));
    assert(!app_button_event(&b,1,640));
    assert(app_button_event(&b,1,670));
    assert(app_flash_count(APP_STATUS_FAILED)==3 && app_flash_duration(APP_STATUS_FAILED)==500);
    assert(app_flash_on_ms(APP_STATUS_CONFIG)==200);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    primitives(); init();
    if (!strcmp(argv[1],"normal")) {
        press(APP_RETRIEVE); pump(100); assert(!communication_snapshot().configured);
        configured(); press(APP_RETRIEVE); pump(1500); valid_log(100,1);
        press(APP_CLEAR); pump(1000);
        press(APP_RETRIEVE); pump(1000); valid_log(0,2);
    } else if (!strcmp(argv[1],"command_timeout")) {
        press(APP_CONFIG); pump(2200);
        assert(communication_snapshot().state==P2_FAILED);
        assert(!communication_snapshot().configured && config_sends==4);
        pump(700); configured();
    } else if (!strcmp(argv[1],"receive_timeout")) {
        configured(); press(APP_RETRIEVE); pump(2700);
        communication_snapshot_t s=communication_snapshot();
        assert(s.state==P2_FAILED && !s.valid && !s.length && !s.completions);
        pump(500); press(APP_RETRIEVE); pump(1500); valid_log(100,1);
    } else if (!strcmp(argv[1],"lost_end_ack")) {
        configured(); press(APP_RETRIEVE); pump(1500); valid_log(100,1);
        assert(end_sends==2 && final_acks==1);
        pump(2000); valid_log(100,1);
    } else if (!strcmp(argv[1],"overflow_recovery")) {
        configured(); press(APP_RETRIEVE);
        communication_step(); sensor_emulator_step();
        rx_ring_fault(&channels[0],RX_FAULT_OVERFLOW);
        pump(2400);
        assert(communication_snapshot().state==P2_FAILED && !communication_snapshot().valid);
        assert(!communication_snapshot().completions);
        press(APP_RETRIEVE); pump(1500); valid_log(100,1);
    } else if (!strcmp(argv[1],"corrupt_frame")) {
        configured(); corrupt_first_data=true;
        press(APP_RETRIEVE); pump(2000); valid_log(100,1);
        assert(corrupted);
    } else if (!strcmp(argv[1],"bounded_queue")) {
        app_message_t m={.kind=APP_CONFIG,.at_ms=now};
        for(unsigned i=0;i<APP_QUEUE_DEPTH;++i) { assert(rt_command_send(&m)); }
        assert(!rt_command_send(&m)); pump(1100);
        assert(config_sends==1 && communication_snapshot().configured);
    } else { assert(!"unknown case"); }
    printf("PASS: UART app host model %s (fault=%d)\n",argv[1],UART_FAULT);
    return 0;
}
