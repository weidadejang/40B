#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>

#include "log.h"

void print_log(int level, int errno_flag, const char *fmt, ...)
{
    int errno_save, n;
    char buf[1024];
    char msg_buf[100];
    va_list ap;
    char* threadnr;

    errno_save = errno;

    threadnr = (char*)pthread_getspecific(tls_key_threadnr);

    n = snprintf(buf, sizeof(buf), "[%s] ", threadnr);
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);

    if (errno_flag) {
        strncat(buf, ": ", 1024 - strlen(buf));
        n += 2;

#if (_POSIX_C_SOURCE >= 200112L) && !  _GNU_SOURCE
        strerror_r(errno_save, msg_buf, sizeof(msg_buf));
        strncat(buf, msg_buf, 1024 - strlen(buf));
#else
        strncat(buf, strerror_r(errno_save, msg_buf, sizeof(msg_buf)), 1024 - strlen(buf));
#endif
    }
    if (level >= 0)
        syslog(level, "%s", buf);

    strcat(buf, "\n");
    fputs(buf, stderr);
    fflush(stderr);

    va_end(ap);
}

void _hexbuf(const char* file, const int line, const char* head, uint8_t *buf, size_t size) {
  char _str[MAX_HEX_LINE * 3 + 32] = {0};
  uint16_t _len = size > MAX_HEX_LINE ? MAX_HEX_LINE : size;
  for(int _i = 0; _i != _len; _i ++)
    snprintf(_str + strlen(_str), sizeof(_str) - strlen(_str), "%02x ", buf[_i]);

  print_log(LOG_DEBUG, 0, "%s:%d %s[%d]:%s", file, line, head, size, _str);
}


