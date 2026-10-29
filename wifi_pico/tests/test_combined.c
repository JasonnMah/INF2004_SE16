/* Real protocol + communication/workflow, host queues and Sensor peer.
 * Reuse the regression wire harness; this is not a kernel/RF simulator. */
#define rt_uart_write harness_uart_write
#define main preserved_uart_test_main
#include "test_uart_app.c"
#undef main
#undef rt_uart_write
#include "upload.h"
#include "log_id.h"
static bool suppress_transfer;
void rt_uart_write(unsigned unit,const unsigned char *bytes,unsigned length) {
    protocol_frame_t f;assert(frame_decode(bytes,length,&f));
    if(suppress_transfer && unit==1 && (f.message_type==MSG_DATA || f.message_type==MSG_TRANSFER_END)) return;
    harness_uart_write(unit,bytes,length);
}
static upload_job_t queued;
static int queued_valid, result_ready;
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
    if(!strcmp(argv[1],"timeout")) {
        suppress_transfer=true;press(APP_RETRIEVE);pump(100);
        assert(communication_snapshot().state==P2_RECEIVING_DATA);
        pump(3000);assert(communication_snapshot().state==P2_FAILED);
        assert(!log_workflow_state()&&!queued_valid);
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
