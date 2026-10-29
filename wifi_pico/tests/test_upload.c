/* Check the actual radio-side handoff without pretending to emulate lwIP. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "upload.h"
#include "http_post.h"
static upload_job_t queued;
static unsigned waiting,started,busy,returned;
static int done;
static const unsigned char bytes[]={0,0x7d,0x7e,0xff};
int rt_upload_take(upload_job_t *j) { if(!waiting)return 0;*j=queued;waiting=0;return 1; }
void rt_upload_result_send(int r) { assert(r==1||r==-1);returned++; }
unsigned long long rt_now_ms(void) { return 1000; }
void rt_log(const char *s) { (void)s; }
void rt_numbers(const char *s,unsigned a,unsigned b,unsigned c) { (void)s;(void)a;(void)b;(void)c; }
void http_post_start_data(uint32_t t,const char *ip,unsigned port,const unsigned char *data,unsigned length,const char *id) {
 (void)t;assert(!busy&&!strcmp(ip,"127.0.0.1")&&port==8000);
 assert(data==bytes&&length==sizeof(bytes)&&!strcmp(id,"log-test"));started++;busy=1;
}
void http_post_poll(uint32_t t,unsigned ready) { (void)t;(void)ready; }
unsigned http_post_attempts(void) { return started?1:0; }
unsigned http_post_status(void) { return 201; }
const char *http_post_reason(void) { return "test receipt"; }
int http_post_result(void) { int r=done;done=0;if(r)busy=0;return r; }
int main(void) {
 queued=(upload_job_t){bytes,sizeof(bytes),"log-test"};waiting=1;
 upload_poll(0);assert(returned==1&&!started&&!waiting);
 waiting=1;upload_poll(1);assert(started==1&&busy&&!waiting);
 upload_poll(1);assert(started==1&&returned==1);
 done=-1;upload_poll(1);assert(returned==2&&!busy);
 waiting=1;upload_poll(1);assert(started==2&&busy);
 done=1;upload_poll(1);assert(returned==3&&!busy);
 puts("PASS radio handoff: offline returns ownership, exact pointer/length/ID, completion once, retry accepts same job");
}
