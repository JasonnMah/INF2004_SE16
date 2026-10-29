/* Copy to http_test.local.h (ignored). Never put passwords in chat or Git.
 * First milestone supports a personal 2.4 GHz WPA2 hotspot/router only.
 * Campus username/password (enterprise Wi-Fi) is not implemented. */
#ifndef HTTP_TEST_CONFIG_H
#define HTTP_TEST_CONFIG_H
#define HTTP_TEST_CONFIG_SET 0
#if !HTTP_TEST_CONFIG_SET && !HTTP_BUILD_CHECK
#error "Edit your ignored HTTP settings and set HTTP_TEST_CONFIG_SET to 1."
#endif
#define UTK_WIFI_SSID "your-2.4GHz-hotspot"
#define UTK_WIFI_PASSWORD "replace-this-password"
#define UTK_WIFI_AUTH CYW43_AUTH_WPA2_AES_PSK
/* PC's IPv4 on the SAME hotspot, not localhost or the Pico address. */
#define HTTP_PC_ADDRESS "192.168.1.100"
#define HTTP_PC_PORT 8000
#endif
