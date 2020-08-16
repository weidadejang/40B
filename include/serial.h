#ifndef __SERIAL_H__
#define __SERIAL_H__

#include <time.h>

typedef struct SERIAL {
  unsigned char databits;
  unsigned char stopbits;
  char parity;
  int speed;
} SERIAL;
typedef struct FDSET {
  int comfd;
  int epollfd;
} FDSET;

int com_set_speed(int fd, int speed);
int com_set_parity(int fd, int databits, int stopbits, char parity);

int com_set_mode(int fd, unsigned char dely_time, unsigned char ch_len);

int getsetings(const char *settings, SERIAL *comset);

FDSET serial_init(const char *dev, char *settings, unsigned char dely_time,
                  unsigned char ch_len);
int SerialSend(const int fd, const char *buf, int len);
int SerialRec(FDSET fds, unsigned char *buf, int size, int timeout);
void printbuf(char *head, const char *buf, char *fmt, int len);
int SerialGetLine(FDSET fds, unsigned char *buf, int size, int timeout);
void close_serial(FDSET *fds);
#endif
