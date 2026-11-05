/* Actual Status Task state machine with a manual clock; no hardware claim. */
#include <assert.h>
#include <stdio.h>
#include "app.h"
#include "buttons.h"
static unsigned long long clock_ms;
static unsigned led, count, buzzer, beep_count, expect_failure;
static app_message_t queue[APP_QUEUE_DEPTH];
unsigned long long rt_now_ms(void) { return clock_ms; }
void rt_led_init(void) { led=0; }
void rt_led_set(unsigned value) { assert(value<=1);led=value; }
void rt_buzzer_init(void) { buzzer=0; }
void rt_buzzer_set(unsigned value) {
    assert(value<=1);
    if(value && !buzzer) ++beep_count;
    buzzer=value;
}
void rt_sleep(unsigned ms) { assert(ms==10);clock_ms+=ms; }
int rt_status_take(app_message_t *message) {
    if(!count) return 0;
    *message=queue[0];
    for(unsigned i=1;i<count;++i) queue[i-1]=queue[i];
    --count;return 1;
}
static void send(unsigned kind,unsigned long long time) {
    assert(count<APP_QUEUE_DEPTH);
    queue[count++]=(app_message_t){.kind=kind,.at_ms=time};
}
static void at(unsigned long long time,unsigned expected) {
    clock_ms=time;status_step();assert(led==expected);
    assert(buzzer==(expect_failure && expected));
}
static void reset(unsigned long long time) {
    count=0;clock_ms=time;beep_count=expect_failure=0;status_init();assert(!led && !buzzer);
}
int main(void) {
    reset(0);send(APP_STATUS_TRANSFERRING,0);at(0,1);
    at(99,1);at(100,0);at(200,1);
    send(APP_STATUS_TRANSFERRING,250);at(250,1);at(300,0);
    send(APP_STATUS_RETRIEVE,350);at(350,1);at(499,1);at(500,0);
    at(600,1);at(749,1);at(750,0);at(1500,0);
    reset(2000);send(APP_STATUS_TRANSFERRING,2000);at(2000,1);at(2100,0);
    expect_failure=1;send(APP_STATUS_FAILED,2150);at(2150,1);at(2250,0);at(2350,1);
    at(2450,0);at(2550,1);at(2650,0);at(3000,0);
    assert(beep_count==3 && !buzzer);
    send(APP_STATUS_TRANSFERRING,1000);at(3000,0); /* Expired queue entry. */
    reset(4000);send(APP_STATUS_TRANSFERRING,4000);at(4000,1);
    at(4100,0);at(4200,1);at(5000,0);at(5200,0); /* Missing final event: no sticky LED. */
    const unsigned finite[]={APP_STATUS_CONFIG,APP_STATUS_CLEAR,APP_STATUS_UPLOAD};
    for(unsigned i=0;i<3;++i) {
        reset(6000);send(APP_STATUS_TRANSFERRING,6000);at(6000,1);
        send(finite[i],6010);at(6010,1);
        unsigned duration=app_flash_on_ms(finite[i]);
        at(6010+duration-1,1);at(6010+duration,0);at(8000,0);
    }
    reset(9000);send(APP_STATUS_TRANSFERRING,9000);send(APP_STATUS_UPLOAD,9001);
    at(9010,1);at(9710,0);at(11000,0); /* Later result wins within one queue drain. */
    assert(beep_count==0); /* Busy, success and activity expiry never sound. */
    const unsigned cancel[]={APP_STATUS_CONFIG,APP_STATUS_RETRIEVE,APP_STATUS_CLEAR,
                             APP_STATUS_UPLOAD,APP_STATUS_TRANSFERRING,APP_STATUS_LOADING};
    for(unsigned i=0;i<6;++i) {
        reset(12000);expect_failure=1;send(APP_STATUS_FAILED,12000);at(12000,1);
        assert(beep_count==1);
        expect_failure=0;send(cancel[i],12010);at(12010,1);
        assert(!buzzer && beep_count==1); /* A new operation cancels the tone. */
        at(14000,0);
    }
    reset(15000);expect_failure=1;send(APP_STATUS_FAILED,15000);at(15000,1);
    reset(15010);assert(!buzzer); /* Reinitialization cannot leave a stuck tone. */
    reset(16000);send(APP_STATUS_LOADING,16000);at(16000,1);
    at(16399,1);at(16400,0);
    send(APP_STATUS_LOADING,16500);at(16500,0);at(16799,0);at(16800,1);
    send(APP_STATUS_LOADING,17000);at(17000,1);at(17200,0);
    assert(!beep_count); /* Loading/retry heartbeats remain silent. */
    expect_failure=1;send(APP_STATUS_FAILED,17250);at(17250,1);
    at(17350,0);at(17450,1);at(17550,0);at(17650,1);at(17750,0);
    assert(beep_count==3);at(19000,0);
    reset(20000);send(APP_STATUS_LOADING,20000);at(20000,1);
    send(APP_STATUS_CONFIG,20100);at(20100,1);at(20300,0);at(22000,0);
    assert(!beep_count); /* Success replaces slow loading, without a beep. */
    reset(23000);send(APP_STATUS_LOADING,23000);at(23000,1);
    at(23400,0);at(23800,1);at(24000,0);at(25000,0); /* Lease expiry. */
    send(APP_STATUS_LOADING,23000);at(25000,0); /* Stale heartbeat ignored. */
    reset(26000);send(APP_STATUS_LOADING,26000);at(26000,1);
    send(APP_STATUS_TRANSFERRING,26050);at(26050,1);at(26150,0);
    send(APP_STATUS_LOADING,26160);at(26160,1);at(26559,1);at(26560,0);
    assert(!beep_count); /* Changing blink speed restarts the right phase. */
    puts("PASS GP20 loading: silent 400ms blink, heartbeat phase, fast/slow transitions, expiry, success and three-beep failure");
    puts("PASS GP18 buzzer: three 100ms failure beeps, silent activity/success, cancellation and silent initialization");
    puts("PASS GP6 status: 100ms non-blocking blink, heartbeat phase, preserved result patterns, stale/expired activity and no stuck LED");
}
