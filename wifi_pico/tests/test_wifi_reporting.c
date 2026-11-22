/* Exercise the actual application poll hook with host-only network inputs.
 * No RTOS, radio, or physical DHCP behavior is emulated by these checks. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "cyw43_utk.h"

/* The generated empty headers let this single fixture supply only the network
 * operations used by http_test.c, without copying the course driver. */
typedef struct {
    uint8_t ssid_len, ssid[32], channel;
    int rssi;
    unsigned auth_mode;
} cyw43_ev_scan_result_t;
#define CYW43_LINK_NONET (-2)
struct netif { unsigned up, link_up, supplied; uint32_t address; };
static unsigned netif_is_up(const struct netif *netif) { return netif->up; }
static unsigned netif_is_link_up(const struct netif *netif) { return netif->link_up; }
static unsigned dhcp_supplied_address(const struct netif *netif) { return netif->supplied; }
static const uint32_t *netif_ip4_addr(const struct netif *netif) { return &netif->address; }
static const char *ip4addr_ntoa(const uint32_t *address) { (void)address; return "192.168.137.199"; }

static T_CYW43_UTK_STATUS status;
static unsigned connected, waiting_ip, delayed_ip, progress_messages, lost_link;
static unsigned ready_upload_polls, offline_upload_polls;
static unsigned retries_scheduled, exhausted_messages, retry_messages;
static unsigned long long clock_ms;
static unsigned last_progress_seconds;
static unsigned starting, searching, connecting, search_progress, radio_failures;
void mp_memory_barrir(void) { }
void cyw43_utk_get_status(T_CYW43_UTK_STATUS *destination) { *destination = status; }
unsigned long long rt_now_ms(void) { return clock_ms; }
unsigned http_post_busy(void) { return 0; }
void upload_poll(unsigned ready)
{
    if (ready) ++ready_upload_polls;
    else ++offline_upload_polls;
}
void rt_trace(const char *name, unsigned loops, unsigned unit)
{ (void)name; (void)loops; (void)unit; }
void rt_log(const char *text)
{
    if (strstr(text, "Network ready; Pico IPv4")) ++connected;
    if (strstr(text, "Hotspot joined; waiting for an IP address")) ++waiting_ip;
    if (strstr(text, "IP address assignment is delayed")) ++delayed_ip;
    if (strstr(text, "Hotspot connection lost")) ++lost_link;
    if (strstr(text, "Starting Wi-Fi radio")) ++starting;
    if (strstr(text, "Retry scheduled")) ++retries_scheduled;
    if (strstr(text, "attempts exhausted")) ++exhausted_messages;
}
void rt_numbers(const char *format, unsigned a, unsigned b, unsigned c)
{
    (void)b; (void)c;
    if (strstr(format, "Still waiting for IP address")) {
        ++progress_messages;last_progress_seconds=a;
    }
    if (strstr(format, "Connection attempt")) ++retry_messages;
    if (strstr(format, "Searching for hotspot")) ++searching;
    if (strstr(format, "Connecting to hotspot")) ++connecting;
    if (strstr(format, "Still trying to connect")) ++search_progress;
    if (strstr(format, "Radio setup failed")) ++radio_failures;
}

#include "../http_service.c"

int main(void)
{
    struct netif netif = {.up = 1, .link_up = 1};
    status.ready = status.init_complete = status.join_complete = status.netif_link_up = 1;
    status.scan_complete = status.join_started = 1;
    status.dhcp_complete = 1;
    status.dhcp_result = -3; /* Historical DHCP timeout remains latched. */
    http_service_enable();
    for (unsigned i = 0; i < 1000; ++i) assert(cyw43_utk_app_poll(&netif) == 0);
    assert(waiting_ip == 1 && delayed_ip == 1 && offline_upload_polls == 1000);
    assert(progress_messages == 0 && retries_scheduled == 0);
    /* A joined hotspot is not reported as disconnected or repeatedly rejoined.
     * Waiting users get bounded progress even if startup text was missed. */
    clock_ms = 9999;
    assert(cyw43_utk_app_poll(&netif) == 0 && progress_messages == 0);
    clock_ms = 10000;
    assert(cyw43_utk_app_poll(&netif) == 0 && progress_messages == 1 && last_progress_seconds == 10);
    for (unsigned i=0;i<1000;++i) assert(cyw43_utk_app_poll(&netif)==0);
    assert(progress_messages == 1 && waiting_ip == 1 && delayed_ip == 1);
    /* A COM5 reconnect requests a current report without restarting either
     * the driver or the elapsed wait timer. Only the radio owner prints it. */
    clock_ms=15000;http_service_report_request();
    assert(cyw43_utk_app_poll(&netif)==0);
    assert(waiting_ip==2 && progress_messages==2 && last_progress_seconds==15);

    /* Late DHCP success makes the real interface usable despite the old status.
     * Reproduce many polls: the original code printed a failure on every poll. */
    netif.supplied = 1;
    for (unsigned i = 0; i < 1000; ++i) assert(cyw43_utk_app_poll(&netif) == 0);
    assert(waiting_ip == 2 && delayed_ip == 1);
    assert(connected == 1 && ready_upload_polls == 1000);
    clock_ms+=60000;assert(cyw43_utk_app_poll(&netif)==0 && progress_messages==2);
    http_service_report_request();assert(cyw43_utk_app_poll(&netif)==0 && connected==2);

    /* A real loss must still be visible once, rather than hiding all errors. */
    netif.supplied = 0;
    for (unsigned i = 0; i < 1000; ++i) assert(cyw43_utk_app_poll(&netif) == 0);
    assert(waiting_ip == 3 && delayed_ip == 2);
    netif.supplied = 1;
    for (unsigned i = 0; i < 1000; ++i) assert(cyw43_utk_app_poll(&netif) == 0);
    assert(waiting_ip == 3 && delayed_ip == 2 && connected == 3);
    /* A lost live link must not be called joined merely because the driver's
     * earlier successful join result remains latched. */
    netif.link_up=netif.supplied=0;status.netif_link_up=0;
    for(unsigned i=0;i<1000;++i) assert(cyw43_utk_app_poll(&netif)==0);
    assert(lost_link==1);

    /* Exercise the real retry dispatch beyond the previous three-attempt cap.
     * Simulate the driver starting a scan after each returned retry request. */
    netif.link_up = netif.supplied = 0;
    status.netif_link_up = status.dhcp_complete = 0;
    status.join_result = CYW43_LINK_NONET;
    for (unsigned i = 0; i < 10; ++i) {
        status.join_complete = 1;
        assert(cyw43_utk_app_poll(&netif) == 0);
        clock_ms += 1999;
        assert(cyw43_utk_app_poll(&netif) == 0);
        ++clock_ms;
        assert(cyw43_utk_app_poll(&netif) == 1);
        status.join_complete = 0;
        clock_ms += 5000;
        assert(cyw43_utk_app_poll(&netif) == 0);
    }
    assert(retries_scheduled == 10 && retry_messages == 10 && exhausted_messages == 0);
    /* Late success during the gap cancels a queued retry. */
    status.join_complete = 1;
    assert(cyw43_utk_app_poll(&netif) == 0);
    clock_ms += 1000;
    status.join_result = 0;
    status.netif_link_up = netif.link_up = netif.supplied = 1;
    assert(cyw43_utk_app_poll(&netif) == 0);
    clock_ms += 5000;
    assert(cyw43_utk_app_poll(&netif) == 0);
    /* Heartbeat spacing and elapsed time remain correct across uptime wrap. */
    clock_ms=UINT32_MAX-5000u;netif.supplied=0;
    assert(cyw43_utk_app_poll(&netif)==0);
    unsigned previous_progress=progress_messages;
    clock_ms+=9999;assert(cyw43_utk_app_poll(&netif)==0 && progress_messages==previous_progress);
    ++clock_ms;assert(cyw43_utk_app_poll(&netif)==0 && progress_messages==previous_progress+1 && last_progress_seconds==10);
    status=(T_CYW43_UTK_STATUS){0};netif.link_up=netif.supplied=0;
    for(unsigned i=0;i<1000;++i)assert(cyw43_utk_app_poll(&netif)==0);
    assert(starting==1);
    unsigned previous_search_progress=search_progress, previous_searching=searching;
    status.init_complete=status.ready=status.scan_active=1;
    for(unsigned i=0;i<1000;++i)assert(cyw43_utk_app_poll(&netif)==0);
    assert(searching==previous_searching+1 && search_progress==previous_search_progress);
    clock_ms+=10000;assert(cyw43_utk_app_poll(&netif)==0 && search_progress==previous_search_progress+1);
    status.scan_active=0;status.scan_complete=status.join_started=1;
    unsigned previous_connecting=connecting;
    for(unsigned i=0;i<1000;++i)assert(cyw43_utk_app_poll(&netif)==0);
    assert(connecting==previous_connecting+1);
    status.init_result=-5;
    for(unsigned i=0;i<1000;++i)assert(cyw43_utk_app_poll(&netif)==0);
    assert(radio_failures==1);
    puts("PASS actual Wi-Fi reporting: joined/IP-wait/online/lost-link stages, late DHCP success, bounded 10-second progress and uptime wrap");
    puts("PASS COM5 reopen refreshes current status without resetting the wait timer or restarting the network");
    puts("PASS upload owner still receives every ready/offline poll; UART/HTTP policies unchanged");
    puts("PASS actual Wi-Fi application poll: retries past attempt three at two-second gaps; in-flight scans and recovered links are not restarted");
    return 0;
}
