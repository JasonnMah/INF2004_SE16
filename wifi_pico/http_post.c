/* One bounded raw-TCP transaction. Callbacks and poll share the radio task.
 * Application buffers are static; lwIP uses its configured fixed-size pools.
 * The same immutable body and ID survive every retry, including a lost reply. */
#include "http_post.h"
#include <string.h>
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"

enum phase { IDLE, CONNECTING, SENDING, RESPONSE, BACKOFF, FINISH };
static enum phase phase;
static struct tcp_pcb *connection;
static ip_addr_t server;
static unsigned server_port, attempt, status_code, offset, header_length, response_length;
static unsigned connected, receipt, retryable;
static unsigned config_request, config_minutes;
static uint32_t config_timestamp;
static uint32_t started;
static int result, failed;
static const char *reason = "idle";
static unsigned char known_body[HTTP_TEST_LENGTH];
static const unsigned char *body;
static unsigned body_length, batch_request, batch_total;
static char log_id[81], length_text[11];
static char header[384], response[1024];

/* Keep HTTP formatting independent of newlib stdio: this course runtime has
 * no POSIX heap/file syscalls. Every append reserves space for the final NUL. */
static unsigned header_text(const char *text)
{
    unsigned length = (unsigned)strlen(text);
    if (length >= sizeof(header) - header_length) return 0;
    memcpy(header + header_length, text, length);
    header_length += length;
    header[header_length] = 0;
    return 1;
}
static unsigned header_number(unsigned value)
{
    char digits[11];
    unsigned end = sizeof(digits) - 1;
    digits[end] = 0;
    do {
        digits[--end] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    return header_text(digits + end);
}
static unsigned build_header(const char *address, unsigned port)
{
    header_length = 0;
    header[0] = 0;
    if (config_request) {
        return header_text("GET /config HTTP/1.1\r\nHost: ") && header_text(address) &&
            header_text(":") && header_number(port) &&
            header_text("\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n");
    }
    return header_text("POST /logs HTTP/1.1\r\nHost: ") && header_text(address) &&
        header_text(":") && header_number(port) &&
        header_text("\r\nContent-Type: application/octet-stream\r\nContent-Length: ") &&
        header_number(body_length) && header_text("\r\nX-Log-ID: ") && header_text(log_id) &&
        (!batch_request || (header_text("\r\nX-Log-Total: ") && header_number(batch_total))) &&
        header_text("\r\nConnection: close\r\n\r\n");
}

static void fail(const char *why, unsigned retry)
{
    failed = 1;
    retryable = retry;
    reason = why;
}
static unsigned equal_name(const char *a, const char *b)
{
    /* HTTP field names are case insensitive; values below are exact. */
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
    }
    return *a == *b;
}
static unsigned decimal_u32(const char *text, uint32_t *value)
{
    if (!*text) return 0;
    uint32_t n = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' || n > (UINT32_MAX - (unsigned)(*text - '0')) / 10u) return 0;
        n = n * 10u + (unsigned)(*text - '0');
    }
    *value = n;
    return 1;
}
static void parse_receipt(void)
{
    unsigned seen = 0;
    char *line = response, *end = strstr(line, "\r\n");
    if (!end) { fail("malformed response", 0); return; }
    *end = 0;
    if (strlen(line) < 12 || (strncmp(line, "HTTP/1.1 ", 9) && strncmp(line, "HTTP/1.0 ", 9)) ||
        line[9] < '1' || line[9] > '5' || line[10] < '0' || line[10] > '9' ||
        line[11] < '0' || line[11] > '9' || (line[12] && line[12] != ' ')) {
        fail("malformed status", 0); return;
    }
    status_code = (line[9]-'0')*100u + (line[10]-'0')*10u + line[11]-'0';
    if (status_code != 200 && (config_request || status_code != 201)) {
        fail("HTTP failure", status_code >= 500 || status_code == 408 || status_code == 429);
        return;
    }
    line = end + 2;
    while (*line) {
        end = strstr(line, "\r\n");
        if (!end) { fail("malformed headers", 0); return; }
        *end = 0;
        if (!*line) break;
        char *colon = strchr(line, ':');
        if (!colon) { fail("malformed header", 0); return; }
        *colon++ = 0;
        while (*colon == ' ' || *colon == '\t') ++colon;
        unsigned bit = 0;
        if (equal_name(line, "Content-Length")) {
            bit = 1; if (strcmp(colon, "0")) { fail("unexpected response body", 0); return; }
        } else if (config_request) {
            uint32_t value;
            if (equal_name(line, "X-Config-Version")) {
                bit = 2;
                if (strcmp(colon, "1")) { fail("unsupported CONFIG version", 0); return; }
            } else if (equal_name(line, "X-Config-Minutes")) {
                bit = 4;
                if (!decimal_u32(colon, &value) || value < 1 || value > 4095) {
                    fail("invalid PC interval", 0); return;
                }
                config_minutes = value;
            } else if (equal_name(line, "X-Config-Unix-Seconds")) {
                bit = 8;
                if (!decimal_u32(colon, &value) || !value) { fail("invalid PC timestamp", 0); return; }
                config_timestamp = value;
            } else if (equal_name(line, "Transfer-Encoding")) {
                fail("unsupported response framing", 0); return;
            }
        } else if (equal_name(line, "X-Log-ID")) {
            bit = 2; if (strcmp(colon, log_id)) { fail("receipt ID mismatch", 0); return; }
        } else if (equal_name(line, "X-Log-Length")) {
            bit = 4; if (strcmp(colon, length_text)) { fail("receipt length mismatch", 0); return; }
        } else if (equal_name(line, "X-Log-Result")) {
            bit = 8; if (strcmp(colon, "stored") && strcmp(colon, "duplicate")) {
                fail("receipt result mismatch", 0); return;
            }
        } else if (equal_name(line, "Transfer-Encoding")) { fail("unsupported response framing", 0); return; }
        if (seen & bit) { fail("duplicate receipt field", 0); return; }
        seen |= bit;
        line = end + 2;
    }
    if (seen != 15) { fail(config_request ? "missing PC configuration" : "missing receiver receipt", 0); return; }
    receipt = 1;
}
static err_t receive(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t error)
{
    (void)arg;
    if (!p) { if (!receipt) fail("connection closed before receipt", 1); return ERR_OK; }
    if (error != ERR_OK) { pbuf_free(p); fail("TCP receive error", 1); return ERR_OK; }
    if (!receipt && !failed) {
        /* TCP is a byte stream: headers can cross any number of packets. */
        for (struct pbuf *part = p; part && !receipt && !failed; part = part->next) {
            for (unsigned i = 0; i < part->len && !receipt && !failed; ++i) {
                char byte = ((const char *)part->payload)[i];
                if (!byte || response_length + 1 >= sizeof(response)) { fail("response too large/invalid", 0); break; }
                response[response_length++] = byte;
                response[response_length] = 0;
                if (response_length >= 4 && !memcmp(response+response_length-4, "\r\n\r\n", 4)) parse_receipt();
            }
        }
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK; /* Never abort a PCB from inside this callback. */
}
static void tcp_error(void *arg, err_t error)
{
    (void)arg; (void)error;
    connection = NULL; /* lwIP already freed it before calling tcp_err. */
    if (!receipt) fail("TCP connection error", 1);
}
static err_t on_connect(void *arg, struct tcp_pcb *pcb, err_t error)
{
    (void)arg; (void)pcb;
    if (error != ERR_OK) fail("TCP connect failed", 1);
    else connected = 1;
    return ERR_OK;
}
static void release_connection(void)
{
    if (!connection) return;
    tcp_arg(connection, NULL);
    tcp_recv(connection, NULL);
    tcp_err(connection, NULL);
    /* Only used outside callbacks. Abort also handles a full close queue. */
    if (failed || tcp_close(connection) != ERR_OK) tcp_abort(connection);
    connection = NULL;
}
static void begin_attempt(uint32_t now)
{
    ++attempt;
    started = now;
    connected = receipt = offset = response_length = failed = status_code = 0;
    response[0] = 0;
    phase = CONNECTING;
    connection = tcp_new();
    if (!connection) { fail("TCP pool exhausted", 1); return; }
    tcp_arg(connection, NULL);
    tcp_recv(connection, receive);
    tcp_err(connection, tcp_error);
    if (tcp_connect(connection, &server, server_port, on_connect) != ERR_OK) fail("TCP connect failed", 1);
}
static void start_data(uint32_t now, const char *address, unsigned port,
                        const unsigned char *data, unsigned length, const char *id,
                        unsigned batched, unsigned total)
{
    if (http_post_busy()) return;
    result = failed = receipt = attempt = status_code = 0;
    config_request = 0; batch_request = batched; batch_total = total;
    reason = "in progress";
    if (!port || port > 65535 || !ipaddr_aton(address, &server)) {
        result = -1; reason = "invalid PC address/port"; return;
    }
    if ((batched && (length > 4096u || total > 1024u * 1024u || length > total)) ||
        (!data && length) || length > 1024u * 1024u || !id || !*id || strlen(id) >= sizeof(log_id)) {
        result = -1; reason = "invalid body/ID"; return;
    }
    for (const char *p = id; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) {
            result = -1; reason = "invalid ID characters"; return;
        }
    }
    server_port = port;
    body = data; body_length = length;
    strcpy(log_id, id);
    char reversed[11]; unsigned digits = 0, value = length;
    do { reversed[digits++] = (char)('0' + value % 10u); value /= 10u; } while (value);
    for (unsigned i = 0; i < digits; ++i) length_text[i] = reversed[digits - i - 1];
    length_text[digits] = 0;
    if (!build_header(address, port)) {
        result = -1; reason = "request header too large"; return;
    }
    begin_attempt(now);
}
void http_post_start_data(uint32_t now, const char *address, unsigned port,
                          const unsigned char *data, unsigned length, const char *id)
{ start_data(now, address, port, data, length, id, 0, 0); }
void http_post_start_batch(uint32_t now, const char *address, unsigned port,
                           const unsigned char *data, unsigned length, const char *id,
                           unsigned total)
{ start_data(now, address, port, data, length, id, 1, total); }
void http_config_start(uint32_t now, const char *address, unsigned port)
{
    if (http_post_busy()) return;
    result = failed = receipt = attempt = status_code = 0;
    config_request = 1;
    config_minutes = config_timestamp = 0;
    reason = "fetching PC configuration";
    if (!port || port > 65535 || !ipaddr_aton(address, &server)) {
        result = -1; reason = "invalid PC address/port"; return;
    }
    server_port = port;
    body = NULL; body_length = 0;
    if (!build_header(address, port)) {
        result = -1; reason = "request header too large"; return;
    }
    begin_attempt(now);
}
unsigned http_config_minutes(void) { return config_minutes; }
uint32_t http_config_timestamp(void) { return config_timestamp; }
void http_post_start(uint32_t now, const char *address, unsigned port)
{
    if (http_post_busy()) return;
    for (unsigned i = 0; i < sizeof(known_body); ++i) known_body[i] = (unsigned char)i;
    http_post_start_data(now, address, port, known_body, sizeof(known_body), HTTP_TEST_ID);
}
unsigned http_post_busy(void) { return phase != IDLE; }
unsigned http_post_attempts(void) { return attempt; }
unsigned http_post_status(void) { return status_code; }
const char *http_post_reason(void) { return reason; }
int http_post_result(void) { int taken = result; result = 0; return taken; }
void http_post_poll(uint32_t now, unsigned network_ready)
{
    if (phase == IDLE) return;
    if (receipt) phase = FINISH;
    if (!network_ready && phase != FINISH) fail("Wi-Fi unavailable", 1);
    if (failed && phase != BACKOFF && phase != FINISH) {
        release_connection();
        if (retryable && attempt < HTTP_MAX_ATTEMPTS) { phase = BACKOFF; started = now; }
        else { phase = IDLE; result = -1; }
        return;
    }
    if (phase == BACKOFF) {
        if (now - started >= HTTP_BACKOFF_MS) begin_attempt(now);
        return;
    }
    if (phase == FINISH) {
        release_connection(); phase = IDLE; result = 1; reason = config_request ? "PC configuration received" : "receiver confirmed storage"; return;
    }
    if (phase == CONNECTING && connected) { phase = SENDING; started = now; }
    if (phase == CONNECTING && now - started >= HTTP_CONNECT_MS) fail("connect timeout", 1);
    if (phase == SENDING) {
        unsigned total = header_length + body_length;
        if (offset < total) {
            unsigned remaining = offset < header_length ? header_length - offset : total - offset;
            unsigned count = tcp_sndbuf(connection);
            if (count > remaining) count = remaining;
            const void *data = offset < header_length ? (const void *)(header + offset) : body + offset - header_length;
            if (count) {
                err_t error = tcp_write(connection, data, (u16_t)count, TCP_WRITE_FLAG_COPY);
                if (error == ERR_OK) offset += count;
                else if (error != ERR_MEM) fail("TCP send failed", 1);
                (void)tcp_output(connection);
            }
        }
        if (offset == total) phase = RESPONSE;
    }
    /* Sending/backpressure and response share one deadline; no endless trickle. */
    if ((phase == SENDING || phase == RESPONSE) && now - started >= HTTP_RESPONSE_MS) fail("response timeout", 1);
}
