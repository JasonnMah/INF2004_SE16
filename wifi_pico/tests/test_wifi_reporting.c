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
static unsigned failures, connected, unavailable, ready_upload_polls, offline_upload_polls;
static unsigned long long clock_ms;
void mp_memory_barrir(void) { }
void cyw43_utk_get_status(T_CYW43_UTK_STATUS *destination) { *destination = status; }
unsigned long long rt_now_ms(void) { return clock_ms++; }
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
    if (strstr(text, "Connected; Pico IPv4")) ++connected;
    if (strstr(text, "Not ready")) ++unavailable;
}
void rt_numbers(const char *format, unsigned a, unsigned b, unsigned c)
{
    (void)a; (void)b; (void)c;
    if (strstr(format, "Setup failed:")) ++failures;
}

#include "../http_service.c"

int main(void)
{
    struct netif netif = {.up = 1, .link_up = 1};
    status.ready = status.init_complete = status.join_complete = status.netif_link_up = 1;
    status.dhcp_complete = 1;
    status.dhcp_result = -3; /* Historical DHCP timeout remains latched. */
    http_service_enable();
    for (unsigned i = 0; i < 1000; ++i) assert(cyw43_utk_app_poll(&netif) == 0);
    assert(failures == 1 && unavailable == 1 && offline_upload_polls == 1000);

    /* Late DHCP success makes the real interface usable despite the old status.
     * Reproduce many polls: the original code printed a failure on every poll. */
    netif.supplied = 1;
    for (unsigned i = 0; i < 1000; ++i) assert(cyw43_utk_app_poll(&netif) == 0);
    if (failures != 1) {
        printf("FAIL late DHCP recovery: expected 1 historical warning, observed %u; reporting flood reproduced\n", failures);
        return 1;
    }
    assert(connected == 1 && ready_upload_polls == 1000);

    /* A real loss must still be visible once, rather than hiding all errors. */
    netif.supplied = 0;
    for (unsigned i = 0; i < 1000; ++i) assert(cyw43_utk_app_poll(&netif) == 0);
    assert(failures == 2 && unavailable == 2 && offline_upload_polls == 2000);
    netif.supplied = 1;
    for (unsigned i = 0; i < 1000; ++i) assert(cyw43_utk_app_poll(&netif) == 0);
    assert(failures == 2 && connected == 2 && ready_upload_polls == 2000);
    puts("PASS actual Wi-Fi application poll: latched DHCP timeout cannot flood after recovery; real offline errors remain visible once");
    puts("PASS upload owner still receives every ready/offline poll; UART/HTTP policies unchanged");
    return 0;
}
