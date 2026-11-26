#ifndef INF2004_HTTP_POST_H
#define INF2004_HTTP_POST_H
#include <stdint.h>
#define HTTP_TEST_LENGTH 1024u
#define HTTP_TEST_ID "pico-known-1024-v1"
#define HTTP_MAX_ATTEMPTS 3u
#define HTTP_CONNECT_MS 5000u
#define HTTP_RESPONSE_MS 10000u
#define HTTP_BACKOFF_MS 1000u
/* Only the course radio owner calls these. The supplied body must remain
 * immutable until the transaction finishes; the client copies the ID. */
void http_post_start_data(uint32_t now, const char *address, unsigned port,
                          const unsigned char *data, unsigned length, const char *id);
void http_post_start_batch(uint32_t now, const char *address, unsigned port,
                           const unsigned char *data, unsigned length, const char *id,
                           unsigned total_length);
void http_post_start(uint32_t now, const char *address, unsigned port);
void http_post_poll(uint32_t now, unsigned network_ready);
/* Same bounded connection/retry engine, exclusively owned by the radio task.
 * GET /config returns a versioned interval and fresh PC Unix timestamp.
 * Poll/result/status APIs below apply to either kind of transaction. */
void http_config_start(uint32_t now, const char *address, unsigned port);
unsigned http_config_minutes(void);
uint32_t http_config_timestamp(void);
unsigned http_post_busy(void);
unsigned http_post_attempts(void);
unsigned http_post_status(void);
/* 0 while idle/busy, 1 successful receipt, -1 bounded failure. Take once. */
int http_post_result(void);
const char *http_post_reason(void);
#endif
