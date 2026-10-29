/* Host-only bridge: run the actual client against the actual PC receiver.
 * It checks HTTP interoperability, not lwIP/firmware/RF timing. */
#define _POSIX_C_SOURCE 200809L
#include "http_post.h"
#include "lwip/tcp.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static struct tcp_pcb pcb;
static int fd=-1,connecting;
static char pending[4096];
static unsigned queued;
static err_t (*rx)(void *,struct tcp_pcb *,struct pbuf *,err_t);
static err_t (*connected)(void *,struct tcp_pcb *,err_t);
static void (*failed)(void *,err_t);
static void valid(struct tcp_pcb *p) { assert(fd>=0&&p==&pcb); }
struct tcp_pcb *tcp_new(void) { assert(fd<0);fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);fcntl(fd,F_SETFL,O_NONBLOCK);queued=0;connecting=0;return &pcb; }
void tcp_arg(struct tcp_pcb *p,void *arg) { valid(p);(void)arg; }
void tcp_recv(struct tcp_pcb *p,err_t (*cb)(void *,struct tcp_pcb *,struct pbuf *,err_t)) { valid(p);rx=cb; }
void tcp_err(struct tcp_pcb *p,void (*cb)(void *,err_t)) { valid(p);failed=cb; }
err_t tcp_connect(struct tcp_pcb *p,const ip_addr_t *ip,u16_t port,err_t (*cb)(void *,struct tcp_pcb *,err_t)) {
 valid(p);connected=cb;struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(port)};addr.sin_addr.s_addr=ip->unused;
 int result=connect(fd,(struct sockaddr *)&addr,sizeof(addr));
 if(result==0||errno==EINPROGRESS){connecting=1;return ERR_OK;}return -2;
}
err_t tcp_close(struct tcp_pcb *p) { valid(p);close(fd);fd=-1;return ERR_OK; }
void tcp_abort(struct tcp_pcb *p) { (void)tcp_close(p); }
u16_t tcp_sndbuf(struct tcp_pcb *p) { valid(p);return sizeof(pending)-queued; }
err_t tcp_write(struct tcp_pcb *p,const void *bytes,u16_t length,unsigned flags) {
 valid(p);assert(flags==TCP_WRITE_FLAG_COPY);if(length>sizeof(pending)-queued)return ERR_MEM;
 memcpy(pending+queued,bytes,length);queued+=length;return ERR_OK;
}
err_t tcp_output(struct tcp_pcb *p) {
 valid(p);if(!queued)return ERR_OK;
 ssize_t sent=send(fd,pending,queued,MSG_NOSIGNAL);
 if(sent>0){memmove(pending,pending+sent,queued-sent);queued-=sent;return ERR_OK;}
 return errno==EAGAIN||errno==EWOULDBLOCK?ERR_MEM:-2;
}
void tcp_recved(struct tcp_pcb *p,u16_t length) { valid(p);(void)length; }
void pbuf_free(struct pbuf *p) { (void)p; }
int ipaddr_aton(const char *s,ip_addr_t *ip) { return inet_pton(AF_INET,s,&ip->unused)==1; }
static uint32_t clock_ms(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)((uint64_t)t.tv_sec*1000+t.tv_nsec/1000000); }
int main(int argc,char **argv) {
 static unsigned char body[65536];
 assert(argc==2||argc==4);
 if(argc==4) {
  FILE *f=fopen(argv[2],"rb");assert(f);unsigned n=(unsigned)fread(body,1,sizeof(body),f);assert(feof(f));fclose(f);
  http_post_start_data(clock_ms(),"127.0.0.1",(unsigned)atoi(argv[1]),body,n,argv[3]);
 } else http_post_start(clock_ms(),"127.0.0.1",(unsigned)atoi(argv[1]));
 uint32_t deadline=clock_ms();
 while(http_post_busy()&&clock_ms()-deadline<60000u) {
  if(fd>=0&&connecting) {
   struct pollfd state={.fd=fd,.events=POLLOUT};
   if(poll(&state,1,0)>0){int error=0;socklen_t size=sizeof(error);getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size);connecting=0;
    if(error){close(fd);fd=-1;failed(NULL,-2);}else connected(NULL,&pcb,ERR_OK);}
  }
  if(fd>=0&&!connecting){
   (void)tcp_output(&pcb);char buffer[113];ssize_t count=recv(fd,buffer,sizeof(buffer),0);
   if(count>0){struct pbuf p={NULL,buffer,(u16_t)count,(u16_t)count};rx(NULL,&pcb,&p,ERR_OK);}
   else if(count==0){close(fd);fd=-1;failed(NULL,-3);}
  }
  http_post_poll(clock_ms(),1);
  struct timespec wait={0,1000000};nanosleep(&wait,NULL);
 }
 int result=http_post_result();printf("HOST HTTP result=%d status=%u attempts=%u reason=%s\n",result,http_post_status(),http_post_attempts(),http_post_reason());
 return result==1?0:1;
}
