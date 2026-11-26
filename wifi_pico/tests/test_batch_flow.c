/* Real shared endpoints + real gateway/journal with queued UART and NOR model.
 * This establishes software behavior, not physical SMP/DMA/flash timing. */
#include "batch_gateway.h"
#include "batch/batch_sensor.h"
#include "protocol/log_digest.h"
#include "protocol/crc16.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static batch_gateway_t gateway;
static batch_sensor_t sensor;
static uint8_t flash[BATCH_STORE_BYTES], log_bytes[UART_BATCH_MAX_LOG], pc_bytes[UART_BATCH_MAX_LOG];
static uint8_t digest[32], pc_present[256], frozen_hash[32];
static unsigned log_length, frozen_length, source_opens, complete_count, receipts;
static unsigned upload_wait, upload_length, upload_offset, upload_calls, duplicate_calls;
static unsigned lost_receipt, drop_type, drop_count, drop_end_ack, end_dropped;
static unsigned flash_calls, flash_fail_at;
static const uint8_t *upload_body;
static char upload_key[80];
static bool frozen;
static uint64_t now;
typedef struct { protocol_frame_t frame; unsigned sensor_bound; } envelope_t;
static envelope_t queue[64];
static unsigned head, tail;
static uint64_t clock_ms(void *ctx) { (void)ctx; return now; }
static void write_wire(void *ctx, const uint8_t *bytes, size_t length)
{
    protocol_frame_t f; assert(frame_decode(bytes,length,&f));
    bool to_sensor = ctx == (void *)1;
    if (f.message_type == drop_type && drop_count) { --drop_count; return; }
    if (to_sensor && drop_end_ack && sensor.protocol.transfer_state == P1_WAIT_END_ACK &&
        f.message_type == MSG_ACK) { drop_end_ack=0; ++end_dropped; return; }
    assert(tail-head < 64);
    queue[tail++ % 64]=(envelope_t){f,to_sensor};
}
static protocol_link_t link(bool gateway_side)
{ return (protocol_link_t){.context=gateway_side?(void *)1:(void *)2,
    .session_id=0x11223344,.write=write_wire,.now_ms=clock_ms}; }
static void drain(void)
{
    unsigned count=0;
    while (head != tail) {
        assert(++count < 20000);
        envelope_t e=queue[head++ % 64];
        if (e.sensor_bound) batch_sensor_handle(&sensor,&e.frame);
        else batch_gateway_frame(&gateway,&e.frame);
    }
    head=tail=0;
}
static bool erase(void *ctx,uint32_t offset)
{
    (void)ctx; assert(offset%4096==0 && offset+4096<=sizeof(flash));
    if (++flash_calls == flash_fail_at) { memset(flash+offset,0xff,71); return false; }
    memset(flash+offset,0xff,4096); return true;
}
static bool program(void *ctx,uint32_t offset,const uint8_t *bytes)
{
    (void)ctx; assert(offset%256==0 && offset+256<=sizeof(flash));
    unsigned n=++flash_calls==flash_fail_at?71:256;
    for (unsigned i=0;i<n;++i) { assert((flash[offset+i]&bytes[i])==bytes[i]);flash[offset+i]&=bytes[i]; }
    return n==256;
}
static batch_flash_t port(void)
{ return (batch_flash_t){.mapped=flash,.erase=erase,.program=program}; }
static bool open_range(void *ctx,const batch_info_t *req,batch_info_t *info,uint8_t *out)
{
    (void)ctx; ++source_opens;
    if (!frozen) {
        frozen_length=batch_digest_zero(req->sha256)?log_length:req->total;
        if (frozen_length>log_length) return false;
        log_digest(log_bytes,frozen_length,frozen_hash);
        if (!batch_digest_zero(req->sha256) && memcmp(req->sha256,frozen_hash,32)) return false;
        frozen=true;
    }
    if (!batch_digest_zero(req->sha256) &&
        (req->total!=frozen_length || memcmp(req->sha256,frozen_hash,32))) return false;
    *info=*req; memcpy(info->sha256,frozen_hash,32);info->total=frozen_length;
    if (info->offset>info->total) return false;
    unsigned rest=info->total-info->offset;
    info->length=rest<4096?rest:4096;
    memcpy(out,log_bytes+info->offset,info->length);
    info->crc=crc16_x25(out,info->length);
    return true;
}
static bool finish_source(void *ctx,const uint8_t *sha,uint32_t total)
{
    (void)ctx;
    uint8_t actual[32];
    if (total>log_length) return false;
    log_digest(log_bytes,total,actual);
    if (memcmp(sha,actual,32)) return false;
    frozen=false; return true; /* Original Sensor log is never erased. */
}
static pico1_backend_t storage;
static void sensor_restart(void)
{
    frozen=false;
    assert(batch_sensor_init(&sensor,link(false),&storage,
        (batch_source_t){.open=open_range,.finish=finish_source}));
}
static bool upload(void *ctx,const uint8_t *body,unsigned length,const char *id,unsigned total)
{
    (void)ctx;
    assert(!upload_wait && total==frozen_length && length<=4096 && strlen(id)==79);
    assert(!strncmp(id,"batch-",6));
    unsigned offset=0;
    for (unsigned i=71;i<79;++i) {
        char c=id[i]; offset=offset*16+(unsigned)(c<='9'?c-'0':c-'a'+10);
    }
    char expected[65];
    for (unsigned i=0;i<32;++i) sprintf(expected+2*i,"%02x",frozen_hash[i]);
    assert(!memcmp(id+6,expected,64) && !memcmp(body,log_bytes+offset,length));
    assert(gateway.store.record.state==BATCH_READY);
    assert(!memcmp(batch_store_data(&gateway.store),body,length));
    upload_body=body; upload_length=length;upload_offset=offset;strcpy(upload_key,id);
    ++upload_calls;upload_wait=1;return true;
}
static void event(void *ctx,unsigned kind,uint32_t offset,uint32_t total)
{
    (void)ctx;(void)offset;(void)total;
    if (kind==BG_EVENT_COMPLETE) ++complete_count;
}
static void gateway_restart(void)
{
    head=tail=0;upload_wait=0;
    assert(batch_gateway_init(&gateway,link(true),port(),
        (batch_gateway_io_t){.upload=upload,.event=event}));
}
static void pc_store(void)
{
    assert(upload_wait && !memcmp(upload_body,log_bytes+upload_offset,upload_length));
    unsigned index=upload_offset/4096;
    if (pc_present[index]) { ++duplicate_calls; assert(!memcmp(pc_bytes+upload_offset,upload_body,upload_length)); }
    else { memcpy(pc_bytes+upload_offset,upload_body,upload_length);pc_present[index]=1; }
}
static void receipt(void)
{
    pc_store(); ++receipts;upload_wait=0;
    batch_gateway_upload_result(&gateway,lost_receipt?-1:1);
    lost_receipt=0;
}
static void step(void)
{
    ++now; batch_gateway_tick(&gateway);drain();batch_sensor_tick(&sensor);drain();
}
static void until_upload(void)
{ for (unsigned i=0;i<200000 && !upload_wait;++i) step();assert(upload_wait); }
static void run(void)
{
    for (unsigned i=0;i<5000000 && gateway.state!=BG_IDLE;++i) {
        step(); if (upload_wait) receipt();
    }
    assert(gateway.state==BG_IDLE && complete_count==1 && !frozen);
    assert(!memcmp(pc_bytes,log_bytes,frozen_length));
    uint8_t pc_hash[32];log_digest(pc_bytes,frozen_length,pc_hash);assert(!memcmp(pc_hash,digest,32));
}
static void reset(unsigned length)
{
    memset(flash,0xff,sizeof(flash));memset(pc_present,0,sizeof(pc_present));
    memset(pc_bytes,0,sizeof(pc_bytes));
    for (unsigned i=0;i<sizeof(log_bytes);++i) log_bytes[i]=(uint8_t)(i^(i>>8)^(i>>16));
    log_length=length;log_digest(log_bytes,length,digest);
    now=1000;frozen=false;head=tail=upload_wait=complete_count=receipts=upload_calls=duplicate_calls=0;
    lost_receipt=drop_type=drop_count=drop_end_ack=end_dropped=flash_calls=flash_fail_at=0;
    source_opens=0;sensor_restart();gateway_restart();assert(batch_gateway_start(&gateway));
}
int main(void)
{
    reset(8192+19);until_upload();
    assert(sensor.protocol.current_block==511 && gateway.data.data_ack_deferred);
    protocol_frame_t last;assert(frame_decode(sensor.protocol.pending_transfer.frame,
                sensor.protocol.pending_transfer.frame_length,&last));
    for (unsigned i=0;i<3;++i) batch_gateway_frame(&gateway,&last);
    assert(head==tail && gateway.data.received_log_length==4096);
    /* Timeout is suspended while a durable receipt is outstanding. */
    now+=10000;step();assert(sensor.protocol.current_block==511 && gateway.state==BG_HTTP);
    batch_gateway_rx_fault(&gateway);assert(gateway.state==BG_HTTP);
    protocol_frame_t early_end=last;early_end.message_type=MSG_TRANSFER_END;early_end.payload_length=2;
    batch_gateway_frame(&gateway,&early_end);assert(gateway.data.data_ack_deferred);
    receipt();run();assert(upload_calls==3);
    puts("PASS full/short batches: held ACK, duplicates, inactivity suspension, immutable HTTP body, exact assembled bytes");
    reset(0);run();assert(upload_calls==1);
    reset(1);run();reset(4096);run();
    puts("PASS empty, one-byte, exact boundary and terminal handshakes");
    reset(UART_BATCH_MAX_LOG);run();assert(upload_calls==256);
    puts("PASS whole 1 MiB: 256 batches / 131072 DATA frames; bounded buffer and journal rotation");
    reset(8192);until_upload();pc_store();gateway_restart();until_upload();receipt();run();
    assert(duplicate_calls==1);
    puts("PASS Wi-Fi restart with READY cache and lost PC receipt: identical ID/body, duplicate stored once");
    reset(8192);until_upload();lost_receipt=1;receipt();until_upload();receipt();run();
    assert(duplicate_calls==1);
    reset(8192);until_upload();receipt();step();assert(gateway.store.record.state==BATCH_CONFIRMED);
    gateway_restart();run();
    puts("PASS failed receipt auto retry and restart after confirmation before Sensor advances");
    reset(8192);while(gateway.state!=BG_RECEIVE)step();gateway_restart();sensor_restart();run();
    puts("PASS both boards restart during FETCHING: frozen prefix revalidated and same log resumed");
    reset(8192);until_upload();sensor_restart();receipt();run();
    puts("PASS Sensor restart during HTTP: source prefix resumed without deleting flash");
    reset(8192);until_upload();
    for(unsigned i=0;i<4;++i) { now+=UART_BATCH_UPLOAD_ACK_MS+1;step(); }
    assert(!sensor.protocol.pending_transfer.active);
    receipt();run();
    puts("PASS PC outage outlasts Sensor retry budget: confirmed range skipped and next range reopened");
    reset(8192);until_upload();pc_store();flash_fail_at=flash_calls+2;receipt();step();
    assert(gateway.store.record.state==BATCH_CONFIRMED);flash_fail_at=0;gateway_restart();run();
    puts("PASS torn confirmed-data erase: restart finishes cleanup and does not re-fetch confirmed range");
    reset(8192);drop_type=MSG_BATCH_INFO;drop_count=1;drop_end_ack=1;run();assert(end_dropped==1);
    reset(17);drop_type=MSG_BATCH_DONE;drop_count=1;run();
    puts("PASS lost INFO, END ACK and DONE request recover without duplicate completion");
    reset(8192);until_upload();log_length+=256;receipt();run();
    assert(frozen_length==8192);
    puts("PASS sampling append excluded from the frozen log; original Sensor bytes retained");
    reset(8192);until_upload();receipt();step();gateway_restart();sensor_restart();
    log_bytes[1]^=1;for(unsigned i=0;i<30000;++i)step();
    assert(batch_gateway_active(&gateway) && gateway.store.record.offset==0 && upload_calls==1);
    puts("PASS changed Sensor prefix fails closed; confirmed progress is not assigned to another log");
    return 0;
}
