/* Built with a "program image" that reaches into the log region: flog_init must refuse to start. */
#include "test.h"
#include "flash_log.h"
#include "fake_flash.h"

static void test_overlap_is_detected(void) {
    fake_flash_reset();
    CHECK_EQ(flog_init(), FLOG_ERR_OVERLAP);
}

int main(void) {
    RUN(test_overlap_is_detected);
    TESTS_DONE();
}
