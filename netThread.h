#ifndef __NETTHREAD__
#define __NETTHREAD__

#include "main.h"

int RecvMsg(int _fd, int epollfd, unsigned char *buf, int size);
int SendMsg(int _fd, unsigned char *buf, int len);

void start_net_task(void);

#endif
