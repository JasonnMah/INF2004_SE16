#include <assert.h>
#include <stdio.h>
#include "network_retry.h"
int main(void)
{
    network_retry_t retry = {.attempts = 1};
    assert(!network_retry_due(&retry, false, 0));
    assert(!network_retry_due(&retry, true, 100));
    assert(!network_retry_due(&retry, true, 2099));
    assert(network_retry_due(&retry, true, 2100));
    assert(retry.attempts == 2);
    /* In-flight scans/joins must not consume another attempt. */
    assert(!network_retry_due(&retry, false, 2200));
    assert(!network_retry_due(&retry, true, 5000));
    assert(!network_retry_due(&retry, true, 6999));
    assert(network_retry_due(&retry, true, 7000));
    assert(retry.attempts == 3);
    /* An unavailable hotspot must not require a reboot after attempt three. */
    uint32_t failed_at = 7100;
    for (unsigned expected = 4; expected <= 1000; ++expected) {
        assert(!network_retry_due(&retry, true, failed_at));
        assert(!network_retry_due(&retry, true, failed_at + 1999));
        assert(network_retry_due(&retry, true, failed_at + 2000));
        assert(retry.attempts == expected);
        failed_at += 2100;
    }
    retry = (network_retry_t){.attempts = UINT_MAX};
    assert(!network_retry_due(&retry, true, 0));
    assert(network_retry_due(&retry, true, 2000));
    assert(retry.attempts == UINT_MAX);
    /* A successful/late association cancels the failure's old deadline. */
    retry = (network_retry_t){.attempts = 1};
    assert(!network_retry_due(&retry, true, 100));
    assert(!network_retry_due(&retry, false, 1500));
    assert(!network_retry_due(&retry, true, 2100));
    assert(!network_retry_due(&retry, true, 4099));
    assert(network_retry_due(&retry, true, 4100));
    /* Failure detected near the 32-bit clock wrap still waits two seconds. */
    retry = (network_retry_t){.attempts = 1};
    assert(!network_retry_due(&retry, true, UINT32_MAX - 999));
    assert(!network_retry_due(&retry, true, 999));
    assert(network_retry_due(&retry, true, 1000));
    assert(retry.attempts == 2);
    assert(network_join_step(0, 0, 22000, true) == NETWORK_JOIN_WAIT);
    assert(network_join_step(0, 0, 999999, true) == NETWORK_JOIN_WAIT);
    assert(network_join_step(0, 0, 1999999, true) == NETWORK_JOIN_WAIT);
    assert(network_join_step(0, 0, 2000000, true) == NETWORK_JOIN_RESUBMIT);
    assert(network_join_step(0, 0, 2000000, false) == NETWORK_JOIN_WAIT);
    assert(network_join_step(0, 0, WIFI_JOIN_WINDOW_US, true) == NETWORK_JOIN_EXPIRED);
    /* Repeated early NONET must not restart the deadline or flood join calls. */
    uint64_t last_request = 0;
    unsigned resubmissions = 0;
    for (uint64_t now = 22000; now < WIFI_JOIN_WINDOW_US; now += 1000) {
        network_join_action_t action = network_join_step(0, last_request, now, true);
        assert(action != NETWORK_JOIN_EXPIRED);
        if (action == NETWORK_JOIN_RESUBMIT) {
            assert(now - last_request >= WIFI_JOIN_NONET_RETRY_US);
            last_request = now;
            ++resubmissions;
        }
    }
    assert(resubmissions == 14);
    assert(network_join_step(0, last_request, WIFI_JOIN_WINDOW_US, true) == NETWORK_JOIN_EXPIRED);
    /* The monotonic 64-bit microsecond counter may also wrap safely. */
    uint64_t start = UINT64_MAX - 499999;
    assert(network_join_step(start, start, 499999, true) == NETWORK_JOIN_WAIT);
    assert(network_join_step(start, start, 1499999, true) == NETWORK_JOIN_WAIT);
    assert(network_join_step(start, start, 1500000, true) == NETWORK_JOIN_RESUBMIT);
    puts("PASS Wi-Fi retry policy: 1000 attempts, cancellation, saturated count and clock wrap; two-second pacing and fixed 30-second deadline");
}
