#ifndef LOG_ID_H
#define LOG_ID_H
/* Content-based ID: identical compressed snapshots share one PC file, even
 * after reboot. This is deduplication, not authentication or decoding. */
void log_id_make(const unsigned char *data, unsigned length, char id[69]);
#endif
