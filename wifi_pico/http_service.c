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
static volatile unsigned enabled, report_generation;
/* Main only requests a fresh report. The radio owner still reads the network,
 * owns all reporting history, and prints the resulting state in its poll. */
void http_service_report_request(void) { ++report_generation; mp_memory_barrir(); }

enum wifi_stage {
    WIFI_UNREPORTED, WIFI_STARTING, WIFI_SEARCHING, WIFI_CONNECTING,
    WIFI_JOIN_FAILED, WIFI_WAIT_IP, WIFI_FINISHING, WIFI_ONLINE,
    WIFI_LINK_LOST, WIFI_INIT_FAILED
};
static struct {
    enum wifi_stage stage;
    uint32_t entered_ms, progress_ms, generation;
    unsigned was_online, ip_delay_reported;
} connection_report;

static void report_connection(const T_CYW43_UTK_STATUS *network,
                               struct netif *netif, unsigned ready, unsigned attempt)
{
    uint32_t now = (uint32_t)rt_now_ms();
    unsigned joined = netif_is_link_up(netif);
    enum wifi_stage stage;
    if (ready) stage = WIFI_ONLINE;
    else if (network->init_complete && network->init_result) stage = WIFI_INIT_FAILED;
    else if (joined) stage = dhcp_supplied_address(netif) ? WIFI_FINISHING : WIFI_WAIT_IP;
    else if (!network->init_complete) stage = WIFI_STARTING;
    else if (network->join_complete && network->join_result) stage = WIFI_JOIN_FAILED;
    else if (network->scan_active || (!network->scan_complete && !network->join_started))
        stage = WIFI_SEARCHING;
    else if (connection_report.was_online && network->join_complete) stage = WIFI_LINK_LOST;
    else stage = WIFI_CONNECTING;

    unsigned changed = stage != connection_report.stage;
    unsigned refresh = connection_report.generation != report_generation;
    connection_report.generation = report_generation;
    if (changed) {
        connection_report.stage = stage;
        connection_report.entered_ms = connection_report.progress_ms = now;
        connection_report.ip_delay_reported = 0;
    }
    if (changed || refresh) {
        switch (stage) {
        case WIFI_STARTING: rt_log("[WIFI] Starting Wi-Fi radio; uploads paused\n"); break;
        case WIFI_SEARCHING:
            rt_numbers("[WIFI] Searching for hotspot (connection attempt %u)\n",attempt,0,0); break;
        case WIFI_CONNECTING:
            rt_numbers("[WIFI] Connecting to hotspot (connection attempt %u)\n",attempt,0,0); break;
        case WIFI_JOIN_FAILED:
            rt_numbers("[WIFI] Could not join hotspot (code=%d); uploads paused\n",
                       network->join_result,0,0); break;
        case WIFI_WAIT_IP:
            rt_log("[WIFI] Hotspot joined; waiting for an IP address (DHCP). Uploads paused\n");
            break;
        case WIFI_FINISHING:
            rt_log("[WIFI] Hotspot joined; finishing network setup. Uploads paused\n"); break;
        case WIFI_LINK_LOST:
            rt_log("[WIFI] Hotspot connection lost; uploads paused while waiting for the link to return\n"); break;
        case WIFI_INIT_FAILED:
            rt_numbers("[WIFI] Radio setup failed (code=%d); uploads paused\n",
                       network->init_result,0,0); break;
        case WIFI_ONLINE:
            connection_report.was_online = 1;
            rt_log("[WIFI] Network ready; Pico IPv4: ");
            rt_log(ip4addr_ntoa(netif_ip4_addr(netif))); rt_log("\n");
#if APP_BATCHED
            rt_log("[WIFI] Uploads enabled; a saved batch resumes automatically\n");
#elif APP_COMBINED
            rt_log("[WIFI] Network ready for requests; GP21 retrieves first, next press uploads\n");
#else
            rt_log("[READY] Press GP21 to POST 1024 known bytes to the PC\n");
#endif
            break;
        default: break;
        }
    }
    /* A driver's startup DHCP timeout can stay latched after a late lease.
     * Report a delay once, then use live interface readiness to end the wait. */
    if (stage == WIFI_WAIT_IP && network->dhcp_complete && network->dhcp_result &&
        !connection_report.ip_delay_reported) {
        connection_report.ip_delay_reported = 1;
        rt_log("[WIFI] IP address assignment is delayed; hotspot is joined and DHCP is still being checked\n");
    }
    if ((uint32_t)(now - connection_report.progress_ms) < 10000u && !(refresh && !changed)) return;
    unsigned seconds = (uint32_t)(now - connection_report.entered_ms) / 1000u;
    connection_report.progress_ms = now;
    switch (stage) {
    case WIFI_WAIT_IP:
        rt_numbers("[WIFI] Still waiting for IP address: %u seconds; DHCP requests=%u. Hotspot joined; uploads paused\n",
                   seconds,network->dhcp_tries,0); break;
    case WIFI_SEARCHING:
    case WIFI_CONNECTING:
        rt_numbers("[WIFI] Still trying to connect: %u seconds; connection attempt=%u\n",
                   seconds,attempt,0); break;
    case WIFI_LINK_LOST:
        rt_numbers("[WIFI] Still waiting for hotspot connection: %u seconds; uploads paused\n",seconds,0,0); break;
    case WIFI_FINISHING:
        rt_numbers("[WIFI] Still waiting for network setup: %u seconds; uploads paused\n",seconds,0,0); break;
    default: break;
    }
}

void http_service_enable(void) { mp_memory_barrir(); enabled = 1; }

int cyw43_utk_app_poll(struct netif *netif)
{
    if (!enabled) return 0;
    mp_memory_barrir(); /* IPC objects were fully initialized before publish. */
    static unsigned announced;
#if APP_VERBOSE_DIAGNOSTICS
    static unsigned scan_reported, join_reported, nonet_reported;
#endif
#if !APP_COMBINED
    static unsigned previous_attempt;
#endif
    static network_retry_t retry = {.attempts = 1};
    T_CYW43_UTK_STATUS network;
    cyw43_utk_get_status(&network); /* Hook runs outside status publication. */
    unsigned ready = network.ready && netif_is_up(netif) && netif_is_link_up(netif) && dhcp_supplied_address(netif);
    if (!announced) {
        rt_trace("Wi-Fi/HTTP owner", 1, 2);
        rt_log("[WIFI] Automatic connection retries enabled; retry gap=2000 ms\n");
        announced = 1;
    }
#if APP_VERBOSE_DIAGNOSTICS
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
#endif
    report_connection(&network, netif, ready, retry.attempts);
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
    if (join_failed && !retry.waiting)
        rt_log("[WIFI] Retry scheduled in 2 seconds; a fresh hotspot search will start\n");
    if (network_retry_due(&retry, join_failed, (uint32_t)rt_now_ms())) {
        rt_numbers("[WIFI] Connection attempt %u: fresh scan and join\n",
                   retry.attempts, 0, 0);
        scan_results = target_matches = target_channel = target_auth = 0;
        target_rssi = -32768;
#if APP_VERBOSE_DIAGNOSTICS
        scan_reported = join_reported = nonet_reported = 0;
#endif
        return 1;
    }
    return 0;
}
