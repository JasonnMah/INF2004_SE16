#ifndef PC_CONFIG_H
#define PC_CONFIG_H
/* Copied kernel messages only: no shared writable settings or network calls
 * outside the radio owner. One GP20 request stays outstanding until its result. */
typedef struct {
    unsigned token;
    int result;
    unsigned interval_minutes;
    unsigned unix_seconds;
} pc_config_result_t;
int rt_config_request_send(unsigned token);
int rt_config_request_take(unsigned *token);
void rt_config_result_send(const pc_config_result_t *result);
int rt_config_result_take(pc_config_result_t *result);
#endif
