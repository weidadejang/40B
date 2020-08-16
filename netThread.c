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
#include "netThread.h"
#include "protocol.h"
#include "up_packet.h"

extern pthread_key_t tls_key_threadnr;

static u8_t *recbuf = NULL;
const u32_t recv_buff_size = 1024 * 1024;

struct sock_param{
  int sockfd;
  int epollfd;
} gp;

static TcpContext TcpCtx = {
          .last_edid = 0,
          .up_packet = NULL,
        };

static int net_cmd_do(const char *ip, const int port, TcpContext *TcpCtx);

int RecvMsg(int _fd, int epollfd, unsigned char *buf, int size) {
  int nfds, read_count;
  struct epoll_event events[1];
  int rec_len = 0;

  memset(events, 0, sizeof(events));
  while (rec_len < size) {
    nfds = epoll_wait(epollfd, events, 1, 500);
    if (0 < nfds) {
      read_count = recv(_fd, buf + rec_len, size - rec_len, 0);
      if (read_count < 0 && errno == EINTR)
        continue;
      else if (read_count == 0)
        break;
      else if (read_count < 0)
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
  struct hostent *h;
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
    sleep(3);
    if (cnt++ > systemconf->BACK2) {
    //if (cnt++ > 0) {
      memset(ip, 0, sizeof(ip));
      rwlock_rdlock();
      if (!ip_flag) {
        strncpy(ip, systemconf->DestIP1, sizeof(ip));
        port = systemconf->DestPort1;
      } else {
        //strncpy(ip, systemconf->DestIP2, sizeof(ip));
        //port = systemconf->DestPort2;
      //}

		  h = gethostbyname((char *)systemconf->DestURL);
		  if(h!=NULL)
		  {
			  logger_warning( "Domain-->Get Host Name : = %s\n", h->h_name);
			  logger_warning( "Domain-->IP address: %s",inet_ntoa(*((struct in_addr *)h->h_addr)));
			  strncpy(ip, inet_ntoa(*((struct in_addr *)h->h_addr)), sizeof(ip));
			  port = systemconf->DestPort2;
		  }
		  else
		  {
		        strncpy(ip, systemconf->DestIP1, sizeof(ip));
		        port = systemconf->DestPort1;
		  }
      }


      rwlock_unlock();
      status = net_cmd_do(ip, port, &TcpCtx);
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
    /* TEST sigaction */
    // printf("------send usr sigaction--------\n");
    // kill(getpid(), SIGUSR1);
    // sleep(100);
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


void DateTimeSetting(int year,int month,int day,int hour,int min,int sec)
{
    struct tm tptr;
    struct timeval tv;

    tptr.tm_year = year+100;
    tptr.tm_mon = month - 1;
    tptr.tm_mday = day;
    tptr.tm_hour = hour;
    tptr.tm_min = min;
    tptr.tm_sec = sec;

    tv.tv_sec = mktime(&tptr);
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
}

void GetNowTime(int i)
{
	time_t time_seconds = time(0);
	struct tm now_time;
	localtime_r(&time_seconds, &now_time);

	logger_info("Step %d send:-------------->Set Current Datetime:%d-%d-%d %d:%d:%d",i, now_time.tm_year-100, now_time.tm_mon + 1,
    now_time.tm_mday, now_time.tm_hour, now_time.tm_min, now_time.tm_sec);

}


int net_cmd_do(const char *ip, const int port, TcpContext *TcpCtx) {
  int socket_fd = -1;
  struct sock_param gp;
  System *sysconf = get_system_conf();

  socket_fd = create_socket(&gp, ip, port);
  if (socket_fd < 0) {
    return -1;
  }

  int v1, v2, v3, v4, v5, v6;

  v1 = v2 = v3 = v4 = v5 = v6 = 0;
  rwlock_rdlock();
  if (sysconf->MAC)
    sscanf(sysconf->MAC, "%02x:%02x:%02x:%02x:%02x:%02x", &v1, &v2, &v3, &v4, &v5, &v6);
  rwlock_unlock();
  logger_info("mac:%s",sysconf->MAC);
  time_t t = time(NULL);
  struct tm res;
  localtime_r(&t, &res);
  u8_t heart_packet[] = {0xfa, 0xfa, 0x50, 0x0b, 0x00, v1, v2, v3, v4, v5, v6, res.tm_year - 100, res.tm_mon + 1, res.tm_mday, res.tm_hour, res.tm_min, 0x8c, 0xdc, 0xfb, 0xfb};
  u16_t crc = MBCRC16(heart_packet + 2, sizeof(heart_packet) - 6);
  heart_packet[16] = (u8_t) crc;
  heart_packet[17] = (u8_t) (crc >> 8);

  logger_info("send heart packet.");
  //GetNowTime(1);
  if( SendMsg( socket_fd, heart_packet, sizeof(heart_packet)) < 0 ) {
    logger_error("send heart failed!");
    goto out;
  }
  sleep(2);//服务器可能反应比较慢，延时5s等待

  int len = RecvMsg(socket_fd, gp.epollfd, recbuf, recv_buff_size);
  if (len > 0) logger_hexbuf("TCP RX", recbuf, len);
  /*2020-05-24 -----------------------------------> Change AP Date*/
  if ((len > 5) && recbuf[0] == 0xfc && recbuf[1] == 0xfc && recbuf[8] == 0xfd && recbuf[9] == 0xfd)
  {
	  /*2n 3y 4r 5s 6f 7m*/
	  DateTimeSetting(recbuf[2],recbuf[3],recbuf[4],recbuf[5],recbuf[6],recbuf[7]);
	  //GetNowTime(2);
  }
  if (len > 5 && recbuf[0] == 0xfa && recbuf[1] == 0xfa &&
      recbuf[len - 1] == 0xfb)
  {
		//GetNowTime(3);
		UpPacket *send_buf = analysis_tcp_data(recbuf + 2, len - 2, TcpCtx);
		if (send_buf) {
		SendMsg(socket_fd, send_buf->data, send_buf->len);

		//GetNowTime(4);
		logger_hexbuf("TCP TX", send_buf->data, send_buf->len);
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
