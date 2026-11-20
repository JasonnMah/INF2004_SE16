/* Reuse the SDK/NOR model from the baseline tests; test the actual logger's
 * batching callbacks, including page ownership while sampling continues. */
#define STORAGE_TEST_MAIN baseline_reference_main
#include "test_flash_uart.c"

int main(void)
{
    void *mapped=mmap((void *)(uintptr_t)XIP_BASE,PICO_FLASH_SIZE_BYTES,
        PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    assert(mapped==(void *)(uintptr_t)XIP_BASE);
    memset(mapped,0xff,PICO_FLASH_SIZE_BYTES);restart_application();
    uint8_t buffer[4096], first[4096], hash[32];
    batch_info_t request={.session=123},info;
    assert(sensor_open_batch(NULL,&request,&info,buffer) && info.total==0 && info.length==0);
    assert(sensor_finish_batch(NULL,info.sha256,0));
    assert(uart_configure(NULL,1700000000,60));do_sample();
    clock_us+=60000000;measured_temperature=2625;do_sample();do_flush();
    assert(flog_current_page()==0);
    assert(sensor_open_batch(NULL,&request,&info,buffer));
    assert(info.total==256 && info.length==256 && flog_current_page()==1 && need_start);
    memcpy(first,buffer,256);memcpy(hash,info.sha256,32);
    measured_temperature=3000;clock_us+=60000000;do_sample();
    assert(!memcmp(flog_data(),first,256));
    assert(!uart_configure(NULL,1700000120,60) && uart_clear_begin(NULL)==P1_STORAGE_ERROR);
    request=info;request.session=124;
    assert(sensor_open_batch(NULL,&request,&info,buffer) && !memcmp(buffer,first,256));
    hash[0]^=1;assert(!sensor_finish_batch(NULL,hash,256));hash[0]^=1;
    assert(sensor_finish_batch(NULL,hash,256) && !snapshot_held);
    do_flush();restart_application();
    assert(sensor_open_batch(NULL,&request,&info,buffer) && info.total==256);
    assert(!memcmp(buffer,first,256));assert(sensor_finish_batch(NULL,hash,256));
    puts("PASS actual Sensor: close/advance freezes prefix; CONFIG/CLEAR refused while held; later samples excluded; restart prefix revalidated");
    memset(mapped,0xff,PICO_FLASH_SIZE_BYTES);restart_application();
    for(unsigned i=0;i<18;++i) { do_sample();assert(close_page()); }
    request=(batch_info_t){.session=125};assert(sensor_open_batch(NULL,&request,&info,buffer));
    assert(info.total==18*256 && info.length==4096);memcpy(first,buffer,4096);
    request=info;request.offset=4096;request.session=126;
    assert(sensor_open_batch(NULL,&request,&info,buffer) && info.length==512);
    assert(!memcmp(buffer,flog_data()+4096,512));assert(!memcmp(first,flog_data(),4096));
    assert(sensor_finish_batch(NULL,info.sha256,info.total));
    /* Full region: no truncation to the old 1 KiB endpoint capacity. */
    memset(mapped,0xff,PICO_FLASH_SIZE_BYTES);restart_application();
    uint8_t *region=(uint8_t *)(uintptr_t)(XIP_BASE+PICO_FLASH_SIZE_BYTES-FLOG_REGION_BYTES);
    memset(region,0x35,FLOG_REGION_BYTES);restart_application();
    request=(batch_info_t){.session=127};assert(sensor_open_batch(NULL,&request,&info,buffer));
    assert(info.total==1048576 && info.length==4096);
    request=info;request.offset=1044480;request.session=128;
    assert(sensor_open_batch(NULL,&request,&info,buffer) && info.length==4096);
    assert(sensor_finish_batch(NULL,info.sha256,info.total));
    assert(region[0]==0x35 && region[FLOG_REGION_BYTES-1]==0x35);
    puts("PASS actual Sensor: multi-range tail and full 1 MiB flash prefix; original bytes retained");
    return 0;
}
