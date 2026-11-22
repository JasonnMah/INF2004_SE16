/* Startup association retries only; called by the single radio owner.
 * Keep this policy independent of the driver so its timing is testable.
 */
#ifndef INF2004_NETWORK_RETRY_H
#define INF2004_NETWORK_RETRY_H
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#define WIFI_JOIN_RETRY_GAP_MS 2000u
#define WIFI_JOIN_WINDOW_US 30000000ULL
#define WIFI_JOIN_NONET_RETRY_US 2000000ULL
typedef struct {
    unsigned attempts;
    bool waiting;
    uint32_t failed_at;
} network_retry_t;
/* Initialize attempts=1 for the initial course scan/join. A successful join,
 * an in-progress attempt or a late link-up cancels a scheduled retry.
 */
static inline bool network_retry_due(network_retry_t *retry, bool failed, uint32_t now)
{
    if (!failed) { retry->waiting = false; return false; }
    if (!retry->waiting) {
        retry->waiting = true;
        retry->failed_at = now;
        return false;
    }
    /* Unsigned subtraction also handles the millisecond counter wrapping. */
    if ((uint32_t)(now - retry->failed_at) < WIFI_JOIN_RETRY_GAP_MS) return false;
    retry->waiting = false;
    /* Keep retrying indefinitely; the diagnostic counter must not wrap to 0. */
    if (retry->attempts < UINT_MAX) ++retry->attempts;
    return true;
}
/* The stock SDK reconnects on NONET until its deadline. In our radio task,
 * space requests without sleeping here or extending the original deadline.
 * Success and other errors are handled by the driver owner, before this helper.
 */
typedef enum {
    NETWORK_JOIN_WAIT, NETWORK_JOIN_RESUBMIT, NETWORK_JOIN_EXPIRED
} network_join_action_t;
static inline network_join_action_t network_join_step(uint64_t started_us,
        uint64_t last_request_us, uint64_t now_us, bool nonet)
{
    if (now_us - started_us >= WIFI_JOIN_WINDOW_US) return NETWORK_JOIN_EXPIRED;
    if (nonet && now_us - last_request_us >= WIFI_JOIN_NONET_RETRY_US)
        return NETWORK_JOIN_RESUBMIT;
    return NETWORK_JOIN_WAIT;
}
#endif
