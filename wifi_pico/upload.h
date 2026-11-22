#ifndef UPLOAD_H
#define UPLOAD_H
/* A copied queue descriptor lends this immutable snapshot to the radio task.
 * Communication alone owns the state. Only receipt of a result returns the
 * buffer; same-core placement alone is NOT synchronization. */
typedef struct {
    const unsigned char *data; unsigned length; const char *id;
#if APP_BATCHED
    unsigned total_length; /* Whole-log length; body contains one range only. */
#endif
} upload_job_t;
unsigned upload_network_ready(void);
int rt_upload_send(const upload_job_t *job);
int rt_upload_take(upload_job_t *job);
void rt_upload_result_send(int result);
int rt_upload_result_take(void);
void upload_poll(unsigned ready); /* radio owner only */
void log_workflow_init(void);
void log_workflow_completed(const unsigned char *data, unsigned length);
void log_workflow_poll(void); /* communication only */
int log_workflow_command(unsigned command, unsigned long long pressed_at);
void log_workflow_cleared(void);
unsigned log_workflow_state(void); /* test observation in owner context */
#endif
