
#ifndef __LOG__
#define __LOG__

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <syslog.h>
#include <pthread.h>

extern pthread_key_t tls_key_threadnr;

void print_log(int level, int errno_flag, const char *fmt, ...);
void _hexbuf(const char* file, const int line, const char* head, uint8_t *buf, size_t size);

#define logger_debug(format,...) print_log(LOG_DEBUG, 0, __FILE__":%d "format, __LINE__, ##__VA_ARGS__)
#define logger_info(format,...) print_log(LOG_INFO, 0, __FILE__":%d "format, __LINE__, ##__VA_ARGS__)
#define logger_warning(format,...) print_log(LOG_WARNING, 0, __FILE__":%d "format, __LINE__, ##__VA_ARGS__)
#define logger_error(format,...) print_log(LOG_ERR, 1, __FILE__":%d "format, __LINE__, ##__VA_ARGS__)
#define logger_fatal(format,...) print_log(LOG_ALERT, 1, __FILE__":%d "format, __LINE__, ##__VA_ARGS__)

#define MAX_HEX_LINE (80)
#define logger_hexbuf(head, buf, size) _hexbuf(__FILE__, __LINE__, head, buf, size)

#endif
