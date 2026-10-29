#include "log_id.h"
#include <stdio.h>
int main(void) {
 unsigned char bytes[1024];for(unsigned i=0;i<sizeof(bytes);++i) bytes[i]=(unsigned char)i;
 for(unsigned n=0;n<=sizeof(bytes);++n) { char id[69];log_id_make(bytes,n,id);puts(id); }
 return 0;
}
