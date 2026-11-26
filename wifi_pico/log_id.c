/* Preserve the existing whole-log ID format using the shared digest code. */
#include "log_id.h"
#include "protocol/log_digest.h"
#include <string.h>
void log_id_make(const unsigned char *data, unsigned length, char id[69])
{
    unsigned char digest[32];
    static const char hex[]="0123456789abcdef";
    log_digest(data,length,digest);
    memcpy(id,"log-",4);
    for(unsigned i=0;i<32;++i) { id[4+2*i]=hex[digest[i]>>4];id[5+2*i]=hex[digest[i]&15]; }
    id[68]=0;
}
