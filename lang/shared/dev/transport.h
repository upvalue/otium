#ifndef OTIUM_DEV_TRANSPORT_H
#define OTIUM_DEV_TRANSPORT_H
#include <stdint.h>
int ot_dev_open(const char* path);
void ot_dev_close(void);
int ot_dev_poll(int timeout);
const char* ot_dev_next(void);
int ot_dev_client(void);
void ot_dev_send(int client, const char* text);
uint64_t ot_dev_clock(void);
#endif
