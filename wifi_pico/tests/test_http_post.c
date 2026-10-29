/* Exercise the real client with scripted TCP events, not simulated RP2040 RF. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "http_post.h"
#include "lwip/tcp.h"
static struct tcp_pcb pcb;
static err_t (*recv_cb)(void *,struct tcp_pcb *,struct pbuf *,err_t);
static err_t (*connect_cb)(void *,struct tcp_pcb *,err_t);
static void (*error_cb)(void *,err_t);
static unsigned live, mem_full, refuse, now, sent_length;
static char sent[4096];
static void check_live(struct tcp_pcb *p) { assert(live && p == &pcb); }
struct tcp_pcb *tcp_new(void) { assert(!live); live=1; sent_length=0; return &pcb; }
void tcp_arg(struct tcp_pcb *p,void *arg) { check_live(p); (void)arg; }
void tcp_recv(struct tcp_pcb *p,err_t (*cb)(void *,struct tcp_pcb *,struct pbuf *,err_t)) { check_live(p);recv_cb=cb; }
void tcp_err(struct tcp_pcb *p,void (*cb)(void *,err_t)) { check_live(p);error_cb=cb; }
err_t tcp_connect(struct tcp_pcb *p,const ip_addr_t *ip,u16_t port,err_t (*cb)(void *,struct tcp_pcb *,err_t))
{ check_live(p);(void)ip;assert(port==8000);connect_cb=cb;return refuse ? -2 : ERR_OK; }
err_t tcp_close(struct tcp_pcb *p) { check_live(p);live=0;return ERR_OK; }
void tcp_abort(struct tcp_pcb *p) { check_live(p);live=0; }
u16_t tcp_sndbuf(struct tcp_pcb *p) { check_live(p);return 17; }
err_t tcp_write(struct tcp_pcb *p,const void *bytes,u16_t size,unsigned flags)
{ check_live(p);assert(flags==TCP_WRITE_FLAG_COPY);if(mem_full)return ERR_MEM;
 assert(sent_length+size<sizeof(sent));memcpy(sent+sent_length,bytes,size);sent_length+=size;sent[sent_length]=0;return ERR_OK; }
err_t tcp_output(struct tcp_pcb *p) { check_live(p);return ERR_OK; }
void tcp_recved(struct tcp_pcb *p,u16_t size) { check_live(p);(void)size; }
void pbuf_free(struct pbuf *p) { (void)p; }
int ipaddr_aton(const char *s,ip_addr_t *ip) { (void)ip;return !strcmp(s,"192.168.1.2"); }
static void tick(unsigned ms,unsigned ready) { now+=ms;http_post_poll(now,ready); }
static void begin(void) { assert(!http_post_busy());mem_full=refuse=0;http_post_start(now,"192.168.1.2",8000);assert(http_post_busy()); }
static void connected(void) { assert(connect_cb);assert(connect_cb(NULL,&pcb,ERR_OK)==ERR_OK); }
static void send_request(void) {
 connected();for(unsigned i=0;i<200;++i)tick(1,1);
 char *body=strstr(sent,"\r\n\r\n");assert(body);body+=4;
 assert(sent_length==(unsigned)(body-sent)+HTTP_TEST_LENGTH);
 for(unsigned i=0;i<HTTP_TEST_LENGTH;++i)assert((unsigned char)body[i]==(unsigned char)i);
 assert(strstr(sent,"Content-Type: application/octet-stream\r\n"));
 assert(strstr(sent,"X-Log-ID: " HTTP_TEST_ID "\r\n"));
}
static void reply(const char *s) { /* One byte per pbuf, including delimiters. */
 for(unsigned i=0;i<strlen(s);++i) { struct pbuf p={NULL,(void *)(s+i),1,1};assert(recv_cb(NULL,&pcb,&p,ERR_OK)==ERR_OK); }
 tick(1,1);
}
static const char good[]="HTTP/1.1 201 Created\r\ncontent-length: 0\r\nx-log-id: " HTTP_TEST_ID "\r\nX-Log-Length: 1024\r\nX-Log-Result: stored\r\n\r\n";
static void finish_failed(void) { for(unsigned i=0;i<1000&&http_post_busy();++i)tick(100,1);assert(!http_post_busy());assert(http_post_result()==-1);assert(!live); }
int main(void)
{
 begin();send_request();reply(good);assert(http_post_result()==1);assert(http_post_result()==0);assert(!live);
 puts("PASS fragmented receipt, partial writes and exact 1024 binary bytes");
 begin();send_request();char first[4096];unsigned n=sent_length;memcpy(first,sent,n);
 live=0;error_cb(NULL,-3);tick(1,1);tick(1000,1);assert(http_post_attempts()==2);send_request();
 assert(sent_length==n&&!memcmp(sent,first,n));reply("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nX-Log-ID: " HTTP_TEST_ID "\r\nX-Log-Length: 1024\r\nX-Log-Result: duplicate\r\n\r\n");
 assert(http_post_result()==1);puts("PASS lost reply retry uses identical ID/body, freed-PCB safety, duplicate receipt");
 begin();finish_failed();assert(http_post_attempts()==3);puts("PASS connect timeout exhausts three attempts");
 begin();connected();mem_full=1;finish_failed();assert(http_post_attempts()==3);mem_full=0;
 puts("PASS persistent send backpressure has a bounded timeout");
 begin();send_request();reply("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");finish_failed();assert(http_post_attempts()==1);
 begin();send_request();reply("HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\n\r\n");tick(1000,1);assert(http_post_attempts()==2);send_request();reply(good);assert(http_post_result()==1);
 puts("PASS terminal HTTP 403 and retryable HTTP 503");
 const char *bad[]={
 "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n",
 "HTTP/1.1 201 Created\r\nContent-Length: 0\r\nX-Log-ID: wrong\r\n\r\n",
 "HTTP/1.1 201 Created\r\nContent-Length: 0\r\nX-Log-Length: 1023\r\n\r\n",
 "HTTP/1.1 201 Created\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n",
 "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
 "HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n",
 "NOT HTTP\r\n\r\n"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i){begin();send_request();reply(bad[i]);finish_failed();assert(http_post_attempts()==1);}
 puts("PASS receipt/framing rejection prevents false success (including captive-portal style response)");
 begin();send_request();char big[1100];memset(big,'A',sizeof(big)-1);big[sizeof(big)-1]=0;reply(big);finish_failed();
 puts("PASS oversized response bounded");
 begin();for(unsigned i=0;i<100&&http_post_busy();++i)tick(100,0);assert(http_post_result()==-1);assert(!live);
 begin();send_request();reply(good);assert(http_post_result()==1);puts("PASS network loss exhausts retries and fresh request recovers without reset");
 now=0xfffffff0u;begin();finish_failed();assert(http_post_attempts()==3);puts("PASS uptime wraparound");
 begin();refuse=1;finish_failed();refuse=0;assert(http_post_attempts()==3);puts("PASS TCP immediate failure bounded");
 unsigned char raw[]={0,0x7e,0x7d,0xff};
 http_post_start_data(now,"192.168.1.2",8000,raw,sizeof(raw),"log-generic");
 connected();for(unsigned i=0;i<100;++i)tick(1,1);
 char *payload=strstr(sent,"\r\n\r\n");assert(payload);payload+=4;
 assert(sent_length==(unsigned)(payload-sent)+sizeof(raw)&&!memcmp(payload,raw,sizeof(raw)));
 reply("HTTP/1.1 201 Created\r\nContent-Length: 0\r\nX-Log-ID: log-generic\r\nX-Log-Length: 5\r\nX-Log-Result: stored\r\n\r\n");
 finish_failed();assert(http_post_attempts()==1);
 http_post_start_data(now,"192.168.1.2",8000,NULL,0,"log-empty");
 connected();for(unsigned i=0;i<100;++i)tick(1,1);
 assert(strstr(sent,"Content-Length: 0\r\n"));
 reply("HTTP/1.1 201 Created\r\nContent-Length: 0\r\nX-Log-ID: log-empty\r\nX-Log-Length: 0\r\nX-Log-Result: stored\r\n\r\n");
 assert(http_post_result()==1);
 http_post_start_data(now,"192.168.1.2",8000,raw,sizeof(raw),"bad\r\nheader");assert(http_post_result()==-1&&!http_post_busy());
 puts("PASS generic binary body, mismatched receipt length, empty body and invalid ID rejection");
 puts("HTTP client host tests PASS (no hardware/network claim)");return 0;
}
