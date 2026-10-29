#ifndef WIFI_LAB_CAPTURE_H
#define WIFI_LAB_CAPTURE_H
/* Communication Task owns the buffer until the bounded capture finishes. */
void lab_capture_begin(const unsigned char *bytes, unsigned length, unsigned completion);
void lab_capture_step(void);
void lab_capture_cancel(void);
unsigned lab_capture_active(void);
#endif
