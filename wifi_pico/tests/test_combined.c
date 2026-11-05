/* Real protocol + communication/workflow, host queues and Sensor peer.
 * Reuse the regression wire harness; this is not a kernel/RF simulator. */
#define rt_uart_write harness_uart_write
#define rt_status_send harness_status_send
#define main preserved_uart_test_main
#include "test_uart_app.c"
#undef main
#undef rt_uart_write
#undef rt_status_send
#include "upload.h"
#include "pc_config.h"
#include "log_id.h"
static bool suppress_transfer, drop_config_ack;
static unsigned preamble_frames;
static unsigned activity_events, loading_events, failure_events, config_events;
int rt_status_send(const app_message_t *message) {
    int sent=harness_status_send(message);
    if(sent && message->kind==APP_STATUS_TRANSFERRING) ++activity_events;
    if(sent && message->kind==APP_STATUS_LOADING) ++loading_events;
    if(sent && message->kind==APP_STATUS_FAILED) ++failure_events;
    if(sent && message->kind==APP_STATUS_CONFIG) ++config_events;
    return sent;
}
void rt_uart_write(unsigned unit,const unsigned char *bytes,unsigned length) {
    if(unit==0) {
        assert(length>APP_UART_PREAMBLE_LENGTH);
        for(unsigned i=0;i<APP_UART_PREAMBLE_LENGTH;++i) {
            assert(bytes[i]==APP_UART_PREAMBLE_BYTE);
            /* Deliver every prefix byte to the real Sensor stream parser.
             * Only the existing frame-level test observer sees a stripped view. */
            rx_ring_push(&channels[1],bytes[i]);
        }
        ++preamble_frames;
        bytes+=APP_UART_PREAMBLE_LENGTH;length-=APP_UART_PREAMBLE_LENGTH;
        assert(bytes[0]==SOF);
    }
    protocol_frame_t f;assert(frame_decode(bytes,length,&f));
    if(unit==1 && drop_config_ack && f.message_type==MSG_ACK) return;
    if(suppress_transfer && unit==1 && (f.message_type==MSG_DATA || f.message_type==MSG_TRANSFER_END)) return;
    harness_uart_write(unit,bytes,length);
}
static upload_job_t queued;
static int queued_valid, result_ready;
static unsigned pc_pending,pc_token,pc_minutes=60,pc_time=1700000000u,pc_hold,pc_failure;
int rt_config_request_send(unsigned token) { assert(!pc_pending);pc_pending=1;pc_token=token;return 1; }
int rt_config_result_take(pc_config_result_t *result) {
 if(!pc_pending||pc_hold)return 0;
 *result=(pc_config_result_t){pc_token,pc_failure?-1:1,pc_minutes,pc_time};pc_pending=0;return 1;
}
int rt_upload_send(const upload_job_t *job) { if(queued_valid)return 0;queued=*job;queued_valid=1;return 1; }
int rt_upload_take(upload_job_t *job) { if(!queued_valid)return 0;*job=queued;queued_valid=0;return 1; }
void rt_upload_result_send(int result) { assert(!result_ready);result_ready=result; }
int rt_upload_result_take(void) { int r=result_ready;result_ready=0;return r; }
static void reset(void) { queued_valid=result_ready=0;lose_first_final_ack=final_ack_lost=false;init();now=10;configured(); }
static void retrieve(unsigned n) { external_sensor.logger_data_length=n;press(APP_RETRIEVE);pump(2200);valid_log(n,1);assert(log_workflow_state()==1&&!queued_valid); }
static upload_job_t upload(void) { press(APP_RETRIEVE);pump(20);assert(log_workflow_state()==2);upload_job_t j;assert(rt_upload_take(&j));return j; }
int main(int argc,char **argv)
{
    assert(argc==2);reset();
    if(!strcmp(argv[1],"config_loading")) {
        unsigned before=loading_events,sent=config_sends,failed=failure_events;
        pc_hold=1;press(APP_CONFIG);pump(1100);
        assert(loading_events>=before+4 && config_sends==sent);
        pc_failure=1;pc_hold=0;pump(20);
        assert(failure_events==failed+1 && config_sends==sent);
        before=loading_events;pump(1100);assert(loading_events==before);
        pc_failure=0;drop_config_ack=true;press(APP_CONFIG);pump(1700);
        assert(loading_events>=before+6 && communication_snapshot().state!=P2_FAILED);
        pump(600);assert(communication_snapshot().state==P2_FAILED);
        assert(config_sends==sent+4 && failure_events==failed+2);
        before=loading_events;pump(1100);assert(loading_events==before);
        drop_config_ack=false;unsigned success=config_events;
        press(APP_CONFIG);pump(1100);
        assert(communication_snapshot().state==P2_COMPLETE && config_events==success+1);
        before=loading_events;pump(1100);assert(loading_events==before);
        puts("PASS CONFIG loading events: PC wait, no UART on PC failure, UART retries/exhaustion, final result stops, recovery");return 0;
    }
    if(!strcmp(argv[1],"preamble")) {
        assert(preamble_frames==1); /* Initial CONFIG got exactly one prefix. */
        unsigned before=preamble_frames;
        retrieve(100);
        assert(preamble_frames==before+1+(100+DATA_CHUNK_SIZE-1)/DATA_CHUNK_SIZE+1);
        /* HTTP-only button action sends no UART prefix/frame. */
        before=preamble_frames;(void)upload();assert(preamble_frames==before);
        rt_upload_result_send(1);pump(1100);
        before=preamble_frames;unsigned sent=config_sends;
        drop_config_ack=true;press(APP_CONFIG);pump(3000);
        assert(communication_snapshot().state==P2_FAILED);
        assert(preamble_frames==before+4 && config_sends==sent+4);
        drop_config_ack=false;press(APP_CONFIG);pump(1100);
        assert(communication_snapshot().configured && communication_snapshot().state==P2_COMPLETE);
        before=preamble_frames;press(APP_CLEAR);pump(1100);
        assert(preamble_frames==before+1 && !external_sensor.logger_data_length);
        before=preamble_frames;press(APP_RETRIEVE);pump(1100);valid_log(0,2);
        assert(preamble_frames==before+2); /* REQUEST and empty END ACK. */
        puts("PASS preamble: 32x55 outside unchanged frames; awake Sensor CONFIG/DATA/CLEAR, all retry prefixes, empty log and HTTP-only silence");return 0;
    }
    if(!strcmp(argv[1],"transfer_status")) {
        assert(activity_events==0); /* CONFIG fetch/ACK does not blink as data. */
        retrieve(300);assert(activity_events>0);
        unsigned before=activity_events;pump(1100);assert(activity_events==before);
        (void)upload();assert(activity_events>before);
        before=activity_events;pump(800);assert(activity_events>=before+3);
        rt_upload_result_send(-1);pump(1100);
        before=activity_events;pump(1100);assert(activity_events==before);
        (void)upload();assert(activity_events>before);
        rt_upload_result_send(1);pump(1100);
        before=activity_events;pump(1100);assert(activity_events==before);
        puts("PASS activity events: UART retrieval/HTTP upload, bounded heartbeats, failure/retry/success stop, CONFIG stays quiet");return 0;
    }
    if(!strcmp(argv[1],"pc_config")) {
        retrieve(100);(void)upload();rt_upload_result_send(1);pump(1100);
        unsigned sent=config_sends;
        pc_hold=1;press(APP_CONFIG);pump(100);
        assert(pc_pending&&config_sends==sent);
        press(APP_RETRIEVE);press(APP_CLEAR);press(APP_CONFIG);pump(100);
        assert(pc_pending&&config_sends==sent&&communication_snapshot().completions==1);
        /* RX must still ACK a repeated END while a PC configuration waits. */
        unsigned ack_before=final_acks;
        unsigned char payload[2],encoded[TX_BUFFER_SIZE];
        write_u16_le(payload,(100+DATA_CHUNK_SIZE-1)/DATA_CHUNK_SIZE);
        size_t size=frame_encode(MSG_TRANSFER_END,0,app_protocol_link(1).session_id,
                                end_sequence,payload,sizeof(payload),encoded,sizeof(encoded));
        assert(size);rt_uart_write(1,encoded,(unsigned)size);pump(50);
        assert(final_acks==ack_before+1&&communication_snapshot().completions==1&&pc_pending);
        pc_minutes=1;pc_time=1793491200u;pc_hold=0;pump(1000);
        assert(config_sends==sent+1&&external_sensor.sampling_interval==60&&external_sensor.current_timestamp==pc_time);
        pc_minutes=4095;press(APP_CONFIG);pump(1000);
        assert(config_sends==sent+2&&external_sensor.sampling_interval==245700);
        const unsigned invalid[]={0,4096,0xffffffffu};
        for(unsigned i=0;i<3;++i) {
            pc_minutes=invalid[i];press(APP_CONFIG);pump(1000);
            assert(config_sends==sent+2&&external_sensor.sampling_interval==245700);
        }
        pc_minutes=2;pc_failure=1;press(APP_CONFIG);pump(1000);assert(config_sends==sent+2);
        pc_failure=0;pc_time=0;press(APP_CONFIG);pump(1000);assert(config_sends==sent+2);
        pc_time=1793491300u;press(APP_CONFIG);pump(1000);
        assert(config_sends==sent+3&&external_sensor.sampling_interval==120&&external_sensor.current_timestamp==pc_time);
        puts("PASS GP20 PC configuration: busy commands ignored, 1/4095 minutes converted to seconds, timestamps, invalid/offline no UART send, fresh recovery");return 0;
    }
    if(!strcmp(argv[1],"timeout")) {
        suppress_transfer=true;press(APP_RETRIEVE);pump(100);
        assert(communication_snapshot().state==P2_RECEIVING_DATA);
        pump(3000);assert(communication_snapshot().state==P2_FAILED);
        assert(!log_workflow_state()&&!queued_valid);
        unsigned before=activity_events;pump(1100);assert(activity_events==before);
        suppress_transfer=false;retrieve(300);
        puts("PASS receive inactivity after REQUEST ACK cannot publish upload; fresh attempt recovers");return 0;
    }
    if(!strcmp(argv[1],"oversize")) {
        external_sensor.logger_data_length=APP_RECEIVE_CAPACITY+8;press(APP_RETRIEVE);pump(6000);
        assert(!communication_snapshot().valid&&!log_workflow_state()&&!queued_valid);
        retrieve(300);puts("PASS oversize transfer rejected; fresh retrieval recovers");return 0;
    }
    lose_first_final_ack=true;retrieve(300);assert(final_ack_lost&&end_sends==2&&final_acks==1);
    assert(communication_snapshot().completions==1);
    puts("PASS lost final ACK recovered; one snapshot only");
    if(!strcmp(argv[1],"clear")) {
        press(APP_CLEAR);pump(1100);assert(!log_workflow_state()&&external_sensor.logger_data_length==0);
        press(APP_CLEAR);pump(1100);assert(!log_workflow_state());
        press(APP_RETRIEVE);pump(1100);valid_log(0,2);assert(log_workflow_state()==1);
        upload_job_t j=upload();assert(j.length==0);
        rt_upload_result_send(1);pump(20);assert(!log_workflow_state());
        puts("PASS clear discards only on ACK; repeated empty clear and empty upload valid");return 0;
    }
    if(!strcmp(argv[1],"clear_failure")) {
        press(APP_CLEAR);
        for(unsigned i=0;i<490;++i) { communication_step();now+=1000; }
        assert(communication_snapshot().state==P2_FAILED&&log_workflow_state()==1);
        upload_job_t j=upload();assert(j.length==300&&!memcmp(j.data,external_bytes,300));
        puts("PASS failed CLEAR retains waiting log for upload");return 0;
    }
    press(APP_CONFIG);pump(20);assert(config_sends==1); /* Don't invalidate waiting copy. */
    upload_job_t first=upload();assert(first.length==300&&!memcmp(first.data,external_bytes,300));
    char first_id[69];strcpy(first_id,first.id);
    unsigned char saved[300];memcpy(saved,first.data,300);
    press(APP_CONFIG);press(APP_RETRIEVE);press(APP_CLEAR);pump(1100);
    assert(log_workflow_state()==2&&config_sends==1&&external_sensor.logger_data_length==300&&!queued_valid);
    /* Protocol still ACKs an END while radio holds the separate snapshot. */
    protocol_frame_t end={.message_type=MSG_TRANSFER_END,.session_id=app_protocol_link(1).session_id,.sequence=end_sequence,.payload_length=TRANSFER_END_PAYLOAD_SIZE};
    write_u16_le(end.payload,(300+DATA_CHUNK_SIZE-1)/DATA_CHUNK_SIZE);
    unsigned char wire[TX_BUFFER_SIZE];
    size_t wire_length=frame_encode(end.message_type,end.flags,end.session_id,end.sequence,end.payload,end.payload_length,wire,sizeof(wire));assert(wire_length);
    rt_uart_write(1,wire,(unsigned)wire_length);
    pump(20);assert(communication_snapshot().completions==1&&final_acks==2);
    rx_ring_fault(&channels[0],RX_FAULT_OVERFLOW);pump(20);
    assert(log_workflow_state()==2&&!memcmp(first.data,saved,300));
    puts("PASS all buttons busy during upload; repeated END serviced; RX fault cannot mutate upload bytes");
    rt_upload_result_send(-1);press(APP_CLEAR);pump(20);assert(log_workflow_state()==1);
    upload_job_t retry=upload();assert(!strcmp(retry.id,first_id)&&!memcmp(retry.data,saved,300));
    rt_upload_result_send(1);pump(20);assert(!log_workflow_state());
    assert(external_sensor.logger_data_length==300);
    puts("PASS failed HTTP keeps ID/bytes; success releases copy without Sensor CLEAR; stale press rejected");
    FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite(saved,1,300,f)==300);fclose(f);
    printf("SNAPSHOT_ID=%s\n",first_id);
    return 0;
}
