#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "main.h"
#include "crc.h"
#include "mac.h"
#include "sysinfo.h"
#include "netThread.h"
#include "protocol.h"
#include "up_packet.h"

extern pthread_key_t tls_key_threadnr;

static u8_t *recbuf = NULL;
const u32_t recv_buff_size = 10 * 1024 * 1024;

struct sock_param{
  int sockfd;
  int epollfd;
} gp;

static TcpContext TcpCtx = {
          .last_edid = 0,
          .up_packet = NULL,
        };

static int net_cmd_do(const char *ip, const int port, TcpContext *TcpCtx);
static int net_cmd_do_old(const char *ip, const int port, TcpContext *TcpCtx);

int RecvMsg(int _fd, int epollfd, unsigned char *buf, int size) {
  int nfds, read_count;
  struct epoll_event events[1];
  int rec_len = 0;

  memset(events, 0, sizeof(events));
  while (rec_len < size) {
    nfds = epoll_wait(epollfd, events, 1, 100);
    if (0 < nfds) {
      read_count = recv(_fd, buf + rec_len, size - rec_len, 0);
      if (read_count < 0 && errno == EINTR) {
        break;
      }else if (read_count <= 0)
        return -1;
      rec_len += read_count;
    } else {  //超时返回0
      break;
    }
    if (restart) break;
  }
  return rec_len;
}

int SendMsg(int _fd, unsigned char *buf, int len) {
  int cnt = 0x00, ret, _len = len;
  while (_len > 0) {
    ret = send(_fd, (const char *)buf + cnt, _len, 0);
    if (ret < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    cnt += ret;
    _len -= ret;
    if (_len > 0x00) {
      SLEEP(0, 100);
    }
  }
  return (cnt);
}

void *heart_thread(void *arg) {
  int status = 0, err_cnt = 0;
  int cnt = 0;
  unsigned char ip_flag = 0;
  char ip[32] = {0};
  int port;
  System *systemconf = get_system_conf();

  UNUSED(arg);
  cnt = systemconf->Heart;

  // TCP recv buf
  recbuf = malloc(recv_buff_size);
  MEM_CHECK_RPTR(recbuf);
  // TCP Send buf
  // 由于不知道返回数据的长度,一次申请足够空间,最长的数据帧出现在A表查询
  TcpCtx.up_packet = (UpPacket *)calloc(
      1, sizeof(UpPacket) + MAX_TCP_SEND_PACKET);
  MEM_CHECK_RPTR(TcpCtx.up_packet);
  TcpCtx.up_packet->size = MAX_TCP_SEND_PACKET;
  TcpCtx.up_packet->len = 0;

  pthread_detach(pthread_self());
  pthread_setspecific (tls_key_threadnr, "heartThread");
  logger_info("run...");
  while (!restart) {
    sleep(4);
    if (cnt++ > systemconf->Heart) {
      memset(ip, 0, sizeof(ip));
      rwlock_rdlock();
      if (!ip_flag) {
        socket_resolver(systemconf->DestIP1, ip);
        port = systemconf->DestPort1;
      } else {
        socket_resolver(systemconf->DestIP2, ip);
        port = systemconf->DestPort2;
      }
      rwlock_unlock();
      status = net_cmd_do(ip, port, &TcpCtx);
      //status = net_cmd_do_old(ip, port, &TcpCtx);
      cnt = 0;

      if (status < 0) {
        if (err_cnt++ > 4) {  //根据实际修改
          ip_flag = ~ip_flag;
          err_cnt = 0;
        }
        logger_warning( "connect failed!err_cnt = %d ip_flag = %d", err_cnt, ip_flag);
      } else {
        err_cnt = 0;
      }
    }
  }
  free(recbuf);
  free(TcpCtx.up_packet);
  logger_info("exit...");
  return NULL;
}

int create_socket(struct sock_param *g, const char *ip, int port) {
  struct sockaddr_in serv_addr;

  if (!ip) return -1;
  /*创建socket*/
  if ((g->sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    logger_error("socket");
    return -1;
  }
  logger_info("ip: %s port: %d", ip, port);
  /*设置sockaddr_in 结构体中相关参数*/
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  serv_addr.sin_addr.s_addr = inet_addr(ip);
  if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
    logger_error("inet_addr");
    close(g->sockfd);
    return -1;
  }
  bzero(&(serv_addr.sin_zero), 8);

  struct timeval timeout = {6,0};
  if (setsockopt(g->sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(struct timeval)) != 0)
  {
    logger_error("setsockopt error");
    close(g->sockfd);
    return -1;
  }

  /*调用connect函数主动发起对服务器端的连接*/
  if (connect(g->sockfd, (struct sockaddr *)&serv_addr,
              sizeof(struct sockaddr)) == -1) {
    logger_error("connect");
    close(g->sockfd);
    return -1;
  }
  g->epollfd = epoll_create(1);
  if (g->epollfd < 0) {
    logger_error("epoll_create");
    close(g->sockfd);
    return -1;
  }
  struct epoll_event ev;
  ev.data.fd = g->sockfd;
  ev.events = EPOLLIN;
  if (epoll_ctl(g->epollfd, EPOLL_CTL_ADD, g->sockfd, &ev) < 0) {
    logger_error("epoll_ctl ");
    close(g->sockfd);
    close(g->epollfd);
    return -1;
  }

  return g->sockfd;
}

void GetNowTime(int idx) {
  char buf[32];
  logger_info("[%d]-->Current time:%s", idx, sys_time_string(buf, sizeof(buf)));
}

int send_heart_packet(int socket_fd) {
  System *sysconf = get_system_conf();
  int v1, v2, v3, v4, v5, v6;
  time_t t = time(NULL);
  struct tm res;

  v1 = v2 = v3 = v4 = v5 = v6 = 0;
  rwlock_rdlock();
  if (sysconf->MAC)
    sscanf(sysconf->MAC, "%02x:%02x:%02x:%02x:%02x:%02x", &v1, &v2, &v3, &v4, &v5, &v6);
  rwlock_unlock();
  logger_info("mac:%s",sysconf->MAC);

  //logger_info("this DB Url:%s",(char *)sysconf->DestURL);

  localtime_r(&t, &res);
  u8_t heart_packet[] = {0xfa, 0xfa, 0x80, 0x0c, 0x00, v1, v2, v3, v4, v5, v6, res.tm_year - 100, res.tm_mon + 1, res.tm_mday, res.tm_hour, res.tm_min, (uint8_t)(sysconf->StationVer),0x8c, 0xdc, 0xfb, 0xfb};
//u8_t heart_packet[] = {0xfa, 0xfa, 0x50, 0x0b, 0x00, v1, v2, v3, v4, v5, v6, res.tm_year - 100, res.tm_mon + 1, res.tm_mday, res.tm_hour, res.tm_min, 0x8c, 0xdc, 0xfb, 0xfb};
  u16_t crc = MBCRC16(heart_packet + 2, sizeof(heart_packet) - 6);
  heart_packet[17] = (u8_t) crc;
  heart_packet[18] = (u8_t) (crc >> 8);

  logger_info("send heart packet.");

  //GetNowTime(1);
  if( SendMsg( socket_fd, heart_packet, sizeof(heart_packet)) < 0 ) {
    logger_error("send heart failed!");
    return -1;
  }
  else{
	  //Get_SysTime();
	  //logger_hexbuf("TCP TX", heart_packet, sizeof(heart_packet));
  }
  return 0;
}





int net_cmd_do(const char *ip, const int port, TcpContext *TcpCtx) {
  int socket_fd = -1;
  struct sock_param gp;

  socket_fd = create_socket(&gp, ip, port);
  if (socket_fd < 0) {
    return -1;
  }

  if (send_heart_packet(socket_fd) < 0) {
    goto out;
  }
  sleep(2);//服务器可能反应比较慢，延时5s等待
  
  int len = 0;

  while(1) {

    int ret = RecvMsg(socket_fd, gp.epollfd, recbuf + len, recv_buff_size - len);

    if (ret <= 0) {
  	  //Get_SysTime();
      logger_info("The connection was abnormally interrupted.");
      break;
    }
    len += ret;

    ASSERT(len <= recv_buff_size);
    logger_hexbuf("TCP RX", recbuf, len);

    OnePacket *onepkt = (OnePacket*)recbuf;
    if (onepkt->head != 0xfafa) {
      len = 0;
      continue;
    }
    int tmpLen = onepkt->L.sLen;
    if (onepkt->fun_code == TASK_PIC_CODE) { //局部图,长度4个字节
      tmpLen = onepkt->L.lLen;
    }
    int total_size = sizeof(OnePacket) + tmpLen + sizeof(uint16_t);
    /*
     *              0xfafa funcode len(2B) [..len data..] CRC(2B) 0xfbfb
     *              0xfafa funcode len(4B) [..len data..]         0xfbfb
     * */
    if (total_size == len && recbuf[total_size - 1] == 0xfb && recbuf[total_size - 2] == 0xfb) {
      UpPacket *send_buf = analysis_tcp_data(recbuf + 2, len - 2, TcpCtx);
      if (send_buf) {
        SendMsg(socket_fd, send_buf->data, send_buf->len);
        logger_hexbuf("TCP TX", send_buf->data, send_buf->len);
      }
      len = 0;
      // 短连接模式下发送完数据可以退出了, 长连接模式时注释下文
      break;
    }
    if ((recv_buff_size - len) == 0) {
      logger_info("Data exceeds the maximum length limit of receive cache!!!");
      len = 0;
      break;
    }
  }

out:

  logger_info("close socket");
  close(gp.sockfd);
  close(gp.epollfd);

  return 0;
}

void start_net_task(void)
{
  pthread_t pd;

  pthread_create(&pd, NULL, heart_thread, NULL);
}
