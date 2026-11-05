/* Called only in the course radio owner's poll hook. No UART or UI code
 * touches lwIP. Results transfer snapshot ownership back via kernel IPC. */
#include "app.h"
#include "upload.h"
#include "pc_config.h"
#include "http_post.h"
#include "http_test_config.h"
static unsigned active, last_attempt, config_token;
/* active=1 lends an upload buffer; active=2 fetches copied CONFIG values. */
void upload_poll(unsigned ready)
{
    upload_job_t job;
    if(!active && rt_config_request_take(&config_token)) {
        if(!ready || HTTP_BUILD_CHECK) {
            pc_config_result_t result={.token=config_token,.result=-1};
            rt_log("[CONFIG] Wi-Fi unavailable or build-check image; no UART CONFIG sent\n");
            rt_config_result_send(&result);return;
        }
        active=2;last_attempt=0;
        rt_log("[CONFIG] Fetching saved PC interval and current PC time\n");
        http_config_start((unsigned)rt_now_ms(),HTTP_PC_ADDRESS,HTTP_PC_PORT);
    }
    if(!active && rt_upload_take(&job)) {
        if(!ready || HTTP_BUILD_CHECK) {
            rt_log("[HTTP] Wi-Fi unavailable or build-check image; saved log retained\n");
            rt_upload_result_send(-1);return;
        }
        active=1;last_attempt=0;
        rt_numbers("[HTTP] POST /logs: length=%u ID=",job.length,0,0);
        rt_log(job.id);rt_log("\n");
        http_post_start_data((unsigned)rt_now_ms(),HTTP_PC_ADDRESS,HTTP_PC_PORT,job.data,job.length,job.id);
    }
    if(!active) return;
    http_post_poll((unsigned)rt_now_ms(),ready);
    unsigned attempt=http_post_attempts();
    if(attempt!=last_attempt) {
        last_attempt=attempt;rt_numbers(active==2 ? "[CONFIG] Attempt %u/%u\n" : "[HTTP] Attempt %u/%u\n",attempt,HTTP_MAX_ATTEMPTS,0);
    }
    int result=http_post_result();
    if(result) {
        if(active==2) rt_log(result>0?"[CONFIG] PC settings ready: ":"[CONFIG] Fetch failed: ");
        else rt_log(result>0?"[HTTP] SUCCESS: ":"[HTTP] FAILED: ");
        rt_log(http_post_reason());
        rt_numbers(" (HTTP=%u attempts=%u)\n",http_post_status(),attempt,0);
        if(active==2) {
            pc_config_result_t values={.token=config_token,.result=result,
                .interval_minutes=result>0?http_config_minutes():0,
                .unix_seconds=result>0?http_config_timestamp():0};
            rt_config_result_send(&values);
        } else rt_upload_result_send(result);
        active=0;
    }
}
