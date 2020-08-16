#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/time.h>

#include "log.h"
#include "main.h"
#include "netThread.h"
#include "protocol.h"
#include "up_packet.h"
#include "debugThread.h"

int setnonblocking(int sockfd)
{
    if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFD, 0) | O_NONBLOCK) == -1)
    {
        return -1;
    }
    return 0;
}

void *debugThread(void *arg)
{
  int myport = 20001;
  int listener = -1;

  UNUSED(arg);
  pthread_detach(pthread_self());
  pthread_setspecific (tls_key_threadnr, "debugThread");

  if( (listener = socket( AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    exit(1);
  }

  int reuse = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in my_addr;
  my_addr.sin_family = PF_INET;
  my_addr.sin_port = htons(myport);
  my_addr.sin_addr.s_addr = INADDR_ANY;
  bzero( &(my_addr.sin_zero), 8);

  if ( bind( listener, (struct sockaddr*)&my_addr, sizeof(struct sockaddr)) == -1 ) {
    perror("bind");
    exit(1);
  }

  if (listen(listener, 1) == -1) {
    perror("listen");
    exit(1);
  }

  struct epoll_event ev;
  int epollfd = epoll_create(1);
  if (epollfd < 0) {
    perror("epoll_create");
    close(listener);
    exit(1);
  }

  TcpContext TcpCtx = { 0, NULL};
  // 由于不知道返回数据的长度,一次申请足够空间,最长的数据帧出现在A表查询
  TcpCtx.up_packet = (UpPacket *)calloc(
      1, sizeof(UpPacket) + MAX_TCP_SEND_PACKET);
  if (!TcpCtx.up_packet) {
    perror("calloc");
    close(listener);
    kill(getpid(), SIGUSR1);
    return NULL;
  }
  TcpCtx.up_packet->size = MAX_TCP_SEND_PACKET;
  TcpCtx.up_packet->len = 0;

  uint8_t *recbuf = calloc(1, MAX_DEBUG_RECV_SIZE);
  if (!recbuf) {
    perror("calloc");
    close(listener);
    kill(getpid(), SIGUSR1);
    return NULL;
  }

  // struct timeval timeout = {6,0};
  // if (setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(struct timeval)) != 0)
  // {
  //   logger_error("set accept timeout failed");
  // }

  print_log(LOG_INFO, 0, "run...");
  while(!restart) {
    struct sockaddr_in sin;
    socklen_t socklen = sizeof(struct sockaddr_in);
    int client_fd = -1;
    if( (client_fd = accept( listener, (struct sockaddr*)&sin, &socklen )) < 0 ) {
        perror("accept");
        continue;
    }

    ev.events = EPOLLIN;
    ev.data.fd = client_fd;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, client_fd, &ev);

    logger_info("Receive a connection request.");
    // 登录验证，防止恶意登录
    char *login = "Just bing rich could cure unhappiness.";
    memset(recbuf, 0, MAX_DEBUG_RECV_SIZE);
    int len = RecvMsg(client_fd, epollfd, recbuf, MAX_DEBUG_RECV_SIZE);
    if (len < 0 || (unsigned int)len != strlen(login) || strcmp(login, (char*)recbuf)) {
      epoll_ctl(epollfd, EPOLL_CTL_DEL, client_fd, &ev);
      close(client_fd);
      logger_debug("Authentication failed.");
      continue;
    }

    logger_info("accept client...");
    int timeout_count = 0;
    do{
      len = RecvMsg(client_fd, epollfd, recbuf, MAX_DEBUG_RECV_SIZE);
      if (len > 0) logger_hexbuf("TCP RX", recbuf, len);
      if (len > 5 && recbuf[0] == 0xfa && recbuf[1] == 0xfa &&
          recbuf[len - 1] == 0xfb) {
        struct timeval start_time, end_time;
        gettimeofday(&start_time, NULL);
        timeout_count = 0;

        UpPacket *send_buf = analysis_tcp_data(recbuf + 2, len - 2, &TcpCtx);
        if (send_buf) {
          SendMsg(client_fd, send_buf->data, send_buf->len);
          logger_hexbuf("TCP TX", send_buf->data, send_buf->len);
        }
        gettimeofday(&end_time, NULL);
        long usec = end_time.tv_usec - start_time.tv_usec;
        long sec = end_time.tv_sec - start_time.tv_sec;
        logger_info("analysis_tcp_data time: %ld msec",1000 * sec + usec/ 1000);
      } else if (0 > len) {
        break;
      }

      sleep(1);
      if (++ timeout_count > 400) {
        break;
      } //10分钟无操作则断开连接
    } while(!restart);

    epoll_ctl(epollfd, EPOLL_CTL_DEL, client_fd, &ev);
    close(client_fd);
    logger_info("close client...");
  }

  free(recbuf);
  free(TcpCtx.up_packet);
  logger_info("exit...");
  close( listener );
  return 0;
}

void start_debug_task(void)
{
  pthread_t pd;

  // debugThread(NULL);
  pthread_create(&pd, NULL, debugThread, NULL);
}
