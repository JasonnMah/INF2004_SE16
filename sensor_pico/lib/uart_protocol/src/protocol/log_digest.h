#ifndef PROTOCOL_LOG_DIGEST_H
#define PROTOCOL_LOG_DIGEST_H
/* SHA256 content identity, not authentication. No heap or global mutable state. */
void log_digest(const unsigned char *data, unsigned length, unsigned char digest[32]);
#endif
