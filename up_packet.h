
#ifndef __UP_PACKET__
#define __UP_PACKET__

#include "main.h"
#include "protocol.h"

UpPacket *analysis_tcp_data(u8_t *data, unsigned int size, TcpContext *TcpCtx);

#endif
