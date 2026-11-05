/* Called ONLY by the existing course radio task, after each network poll.
 * UI submits copied messages through kernel IPC. No raw lwIP on core 1. */
#include "app.h"
#include "http_post.h"
#if APP_COMBINED
#include "upload.h"
#endif
#include "network_retry.h"
#include "http_test_config.h"
#include "cyw43_utk.h"
#include "cyw43.h"
#include <string.h>
#include "lwip/netif.h"
#include "lwip/dhcp.h"
extern void mp_memory_barrir(void);
/* Scan records may arrive before USB/IPC is ready. Store only bounded
 * metadata here; the same radio owner prints it later. No network names/keys
 * are logged. Scan counters count results, not unique access points. */
static const unsigned char target_ssid[] = UTK_WIFI_SSID;
static unsigned scan_results, target_matches, target_channel, target_auth;
static int target_rssi = -32768;
void cyw43_utk_app_scan(const cyw43_ev_scan_result_t *result)
{
    if (!result) return;
    ++scan_results;
    if (result->ssid_len != sizeof(target_ssid) - 1 ||
        result->ssid_len > sizeof(result->ssid) ||
        memcmp(result->ssid, target_ssid, result->ssid_len)) return;
    ++target_matches;
    if (result->rssi > target_rssi) {
        target_rssi = result->rssi;
        target_channel = result->channel;
        target_auth = result->auth_mode;
    }
}
static volatile unsigned enabled;
void http_service_enable(void) { mp_memory_barrir(); enabled = 1; }

int cyw43_utk_app_poll(struct netif *netif)
{
    if (!enabled) return 0;
    mp_memory_barrir(); /* IPC objects were fully initialized before publish. */
    static unsigned announced, last_ready = 2, reported_failures, scan_reported, join_reported, nonet_reported;
#if !APP_COMBINED
    static unsigned previous_attempt;
#endif
    static network_retry_t retry = {.attempts = 1};
    T_CYW43_UTK_STATUS network;
    cyw43_utk_get_status(&network); /* Hook runs outside status publication. */
    unsigned ready = network.ready && netif_is_up(netif) && netif_is_link_up(netif) && dhcp_supplied_address(netif);
    if (!announced) {
        rt_trace("Wi-Fi/HTTP owner", 1, 2);
        announced = 1;
    }
    if (network.scan_complete && !scan_reported) {
        scan_reported = 1;
        rt_numbers("[WIFI DIAG] Scan results=%u target_matches=%u ssid_bytes=%u\n",
                   scan_results, target_matches, sizeof(target_ssid) - 1);
        if (target_matches) {
            rt_numbers("[WIFI DIAG] Target channel=%u rssi=%d auth=%u\n",
                       target_channel, target_rssi, target_auth);
        }
    }
    if (network.join_started && !network.join_complete &&
        network.link_status == CYW43_LINK_NONET && !nonet_reported) {
        nonet_reported = 1;
        rt_log("[WIFI] Network not found yet; continuing within the 30-second join window\n");
    }
    if (network.join_complete && !join_reported) {
        join_reported = 1;
        rt_numbers("[WIFI DIAG] Join request=%d link=%d elapsed_ms=%u\n",
                   network.join_request_result, network.link_status, network.join_elapsed_ms);
    }
    if (ready != last_ready) {
        last_ready = ready;
        if (ready) {
            rt_log("[WIFI] Connected; Pico IPv4: ");
            rt_log(ip4addr_ntoa(netif_ip4_addr(netif)));
            rt_log("\n");
#if APP_COMBINED
            rt_log("[WIFI] HTTP available; GP21 retrieves first, next press uploads\n");
#else
            rt_log("[READY] Press GP21 to POST 1024 known bytes to the PC\n");
#endif
        } else rt_log("[WIFI] Not ready; waiting for Wi-Fi and DHCP\n");
    }
    if (ready) reported_failures = 0;
    unsigned failure = network.init_complete && network.init_result ? 1u :
        network.join_complete && network.join_result ? 2u :
        network.dhcp_complete && network.dhcp_result ? 4u : 0u;
    /* Course startup results can retain an earlier DHCP timeout after the
     * live interface recovers. Do not re-report it on every ready poll. */
    if (!ready && failure && !(reported_failures & failure)) {
        reported_failures |= failure;
        rt_numbers("[WIFI] Setup failed: init=%d join=%d dhcp=%d\n",
                   network.init_result, network.join_result, network.dhcp_result);
        if (failure == 2u && retry.attempts < WIFI_JOIN_MAX_ATTEMPTS)
            rt_log("[WIFI] Retry scheduled in 2000 ms\n");
        else if (failure == 2u)
            rt_log("[WIFI] Connection attempts exhausted; check hotspot, then restart Wi-Fi Pico\n");
    }
#if APP_COMBINED
    upload_poll(ready);
#else
    app_message_t command;
    if (rt_command_take(&command)) {
        if (http_post_busy()) rt_log("[HTTP] Busy; press ignored\n");
        else if (HTTP_BUILD_CHECK) rt_log("[HTTP] Build-check image: POST disabled; configure and rebuild\n");
        else if (!ready) {
            rt_numbers("[HTTP] Wi-Fi unavailable: init=%d join=%d dhcp=%d; wait for READY or the final connection result\n",
                       network.init_result, network.join_result, network.dhcp_result);
            app_message_t status = {.kind = APP_STATUS_FAILED};
            if (!rt_status_send(&status)) rt_log("[LED] Status queue full\n");
        } else {
            previous_attempt = 0;
            http_post_start((uint32_t)rt_now_ms(), HTTP_PC_ADDRESS, HTTP_PC_PORT);
            rt_log("[HTTP] POST /logs: length=1024 ID=" HTTP_TEST_ID "\n");
        }
    }
    http_post_poll((uint32_t)rt_now_ms(), ready);
    unsigned attempt = http_post_attempts();
    if (attempt != previous_attempt) {
        previous_attempt = attempt;
        rt_numbers("[HTTP] Attempt %u/%u\n", attempt, HTTP_MAX_ATTEMPTS, 0);
    }
    int result = http_post_result();
    if (result) {
        rt_log(result > 0 ? "[HTTP] SUCCESS: " : "[HTTP] FAILED: ");
        rt_log(http_post_reason());
        rt_numbers(" (HTTP=%u attempts=%u)\n", http_post_status(), attempt, 0);
        app_message_t status = {.kind = result > 0 ? APP_STATUS_RETRIEVE : APP_STATUS_FAILED};
        if (!rt_status_send(&status)) rt_log("[LED] Status queue full\n");
    }

#endif
    bool join_failed = network.init_result == 0 && network.join_complete &&
                       network.join_result != 0 && !network.netif_link_up &&
                       !http_post_busy();
    if (network_retry_due(&retry, join_failed, (uint32_t)rt_now_ms())) {
        rt_numbers("[WIFI] Connection attempt %u/%u: fresh scan and join\n",
                   retry.attempts, WIFI_JOIN_MAX_ATTEMPTS, 0);
        scan_results = target_matches = target_channel = target_auth = 0;
        target_rssi = -32768;
        scan_reported = join_reported = nonet_reported = reported_failures = 0;
        return 1;
    }
    return 0;
}
