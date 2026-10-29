#ifndef HTTP_TCP_TEST_DOUBLE_H
#define HTTP_TCP_TEST_DOUBLE_H
#include <stdint.h>
typedef int err_t;
typedef uint16_t u16_t;
typedef struct { unsigned unused; } ip_addr_t;
struct tcp_pcb { unsigned unused; };
struct pbuf { struct pbuf *next; void *payload; u16_t len, tot_len; };
#define ERR_OK 0
#define ERR_MEM -1
#define TCP_WRITE_FLAG_COPY 1
struct tcp_pcb *tcp_new(void);
void tcp_arg(struct tcp_pcb *, void *);
void tcp_recv(struct tcp_pcb *, err_t (*)(void *, struct tcp_pcb *, struct pbuf *, err_t));
void tcp_err(struct tcp_pcb *, void (*)(void *, err_t));
err_t tcp_connect(struct tcp_pcb *, const ip_addr_t *, u16_t, err_t (*)(void *, struct tcp_pcb *, err_t));
err_t tcp_close(struct tcp_pcb *);
void tcp_abort(struct tcp_pcb *);
u16_t tcp_sndbuf(struct tcp_pcb *);
err_t tcp_write(struct tcp_pcb *, const void *, u16_t, unsigned);
err_t tcp_output(struct tcp_pcb *);
void tcp_recved(struct tcp_pcb *, u16_t);
void pbuf_free(struct pbuf *);
int ipaddr_aton(const char *, ip_addr_t *);
#endif
