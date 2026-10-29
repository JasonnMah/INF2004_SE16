/* Button meaning and snapshot lifetime, owned by Communication Task.
 * State 0: retrieve; 1: press again to upload; 2: radio has borrowed snapshot.
 * Failed uploads retain exactly the same bytes and ID for the next press. */
#include "app.h"
#include "upload.h"
#include "log_id.h"
#include <string.h>
static unsigned state, length;
static unsigned char saved[APP_RECEIVE_CAPACITY];
static char id[69];
static unsigned long long accept_after;
static void status(unsigned kind) {
    app_message_t m={.kind=kind,.at_ms=rt_now_ms()};
    if(!rt_status_send(&m)) rt_log("[STATUS] Queue full; indication dropped\n");
}
void log_workflow_init(void) { state=length=0;accept_after=0; }
unsigned log_workflow_state(void) { return state; }
void log_workflow_completed(const unsigned char *data, unsigned count)
{
    if(state || count>sizeof(saved)) rt_fatal("snapshot ownership",-1);
    memcpy(saved,data,count);length=count;
    log_id_make(saved,length,id);
    state=1;accept_after=rt_now_ms();
    rt_numbers("[LOG] Saved %u compressed bytes. Press GP21 again to upload, or GP22 to clear\n",length,0,0);
}
void log_workflow_poll(void)
{
    int result=rt_upload_result_take();
    if(!result) return;
    if(state!=2) rt_fatal("unexpected upload result",-1);
    state=result>0?0:1;
    accept_after=rt_now_ms();
    rt_log(result>0 ? "[UI] Upload complete; Sensor log retained. GP21 retrieves again\n" :
                      "[UI] Upload failed; saved log retained. GP21 retries upload; GP22 clears\n");
    status(result>0?APP_STATUS_UPLOAD:APP_STATUS_FAILED);
}
int log_workflow_command(unsigned command, unsigned long long pressed_at)
{
    if(state==2 || (accept_after && pressed_at<=accept_after)) {
        rt_log("[UI] Busy/stale press; release and press again when idle\n");return 0;
    }
    if(state==1 && command==APP_CONFIG) {
        rt_log("[UI] Saved log waiting: GP21 uploads; GP22 clears before a new CONFIG\n");return 0;
    }
    if(state==1 && command==APP_RETRIEVE) {
        upload_job_t job={saved,length,id};
        if(rt_upload_send(&job)) { state=2;rt_log("[UI] Uploading saved compressed log\n"); }
        else rt_log("[UI] Upload queue busy; saved log retained\n");
        return 0;
    }
    return 1; /* UART owns CONFIG/retrieval/CLEAR. */
}
void log_workflow_cleared(void)
{
    if(state==2) rt_fatal("clear during upload",-1);
    state=length=0;accept_after=rt_now_ms();
    rt_log("[LOG] Sensor CLEAR acknowledged; waiting copy discarded. PC files unchanged\n");
}
