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
#include "down_packet.h"
#include "debugThread.h"
#include "websocket.h"
#include "base64.h"

// #undef logger_debug
// #undef logger_info
// #undef logger_error

// #define logger_debug(format, ...)
// #define logger_info(format, ...)
// #define logger_error(format, ...)

int setnonblocking(int sockfd)
{
    if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFD, 0) | O_NONBLOCK) == -1)
    {
        return -1;
    }
    return 0;
}

int safeSend(int clientSocket, const uint8_t *buffer, size_t bufferSize)
{
    ssize_t written = send(clientSocket, buffer, bufferSize, 0);
    if (written == -1) {
        close(clientSocket);
        perror("send failed");
        return EXIT_FAILURE;
    }
    if (written != bufferSize) {
        close(clientSocket);
        perror("written not all bytes");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}


int listener = -1;

void terminal(void) {
  if (listener > 0)
    close(listener);
}

void *debugThread(void *arg)
{
  int myport = 802;

  UNUSED(arg);
  pthread_detach(pthread_self());
  pthread_setspecific (tls_key_threadnr, "webSocketThread");

  if( (listener = socket( AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    exit(1);
  }

  int reuse = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in local;
  local.sin_family = PF_INET;
  local.sin_port = htons(myport);
  local.sin_addr.s_addr = INADDR_ANY;
  bzero( &(local.sin_zero), 8);

  if ( bind( listener, (struct sockaddr*)&local, sizeof(struct sockaddr)) == -1 ) {
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

  if (!init_global_buf(MAX_DEBUG_RECV_SIZE)) {
    perror("calloc");
    close(listener);
    kill(getpid(), SIGUSR1);
    return NULL;
  }

  char *recbuf = calloc(1, MAX_DEBUG_RECV_SIZE);
  if (!recbuf) {
    perror("calloc");
    destory_temp_buf();
    close(listener);
    kill(getpid(), SIGUSR1);
    return NULL;
  }

  logger_info("opened %s:%d", inet_ntoa(local.sin_addr), ntohs(local.sin_port));
  // struct timeval timeout = {6,0};
  // if (setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(struct timeval)) != 0)
  // {
  //   logger_error("set accept timeout failed");
  // }

  logger_info("run...");
  logger_info("listen on %d port!", myport);
  while(!restart) {
    logout(); // 默认 logout 状态

    struct sockaddr_in remote;
    socklen_t socklen = sizeof(struct sockaddr_in);
    int client_fd = -1;
    if( (client_fd = accept( listener, (struct sockaddr*)&remote, &socklen )) < 0 ) {
      restart = 1;
      continue;
    }

    ev.events = EPOLLIN;
    ev.data.fd = client_fd;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, client_fd, &ev);

    logger_info("connected %s:%d", inet_ntoa(remote.sin_addr), ntohs(remote.sin_port));
    logger_info("accept client...");


    enum wsState state = WS_STATE_OPENING;
    int timeout_count = 0;
    int len = 0;
    size_t frameSize = MAX_DEBUG_RECV_SIZE;
    uint8_t *data = NULL;
    size_t dataSize = 0;
    enum wsFrameType frameType = WS_INCOMPLETE_FRAME;
    struct handshake hs;
    nullHandshake(&hs);

    #define prepareBuffer frameSize = MAX_DEBUG_RECV_SIZE; memset(recbuf, 0, MAX_DEBUG_RECV_SIZE);
    #define initNewFrame frameType = WS_INCOMPLETE_FRAME; len = 0; memset(recbuf, 0, MAX_DEBUG_RECV_SIZE);

    do{
      ssize_t readed = RecvMsg(client_fd, epollfd, recbuf + len, MAX_DEBUG_RECV_SIZE - len);
      if (readed < 0) { // 链接中断
        break;
      } else if (!readed) {
        continue;
      }
      len += readed;
      assert(len <= MAX_DEBUG_RECV_SIZE);
      logger_hexbuf("TCP RX", recbuf, len);
      switch (state) {
        case WS_STATE_OPENING:
          frameType = wsParseHandshake(recbuf, len, &hs);
          if (frameType == WS_OPENING_FRAME) {
            initNewFrame;
            if (strcmp(hs.resource, "/") != 0) {
              frameSize = sprintf((char *)recbuf, "HTTP/1.1 404 Not Found\r\n\r\n");
              safeSend(client_fd, recbuf, frameSize);
              goto errout;
            }
            prepareBuffer;
            wsGetHandshakeAnswer(&hs, recbuf, &frameSize);
            freeHandshake(&hs);
            if (safeSend(client_fd, recbuf, frameSize) == EXIT_FAILURE)
              goto errout;
            state = WS_STATE_NORMAL;
            break;
          }

          if ((frameType == WS_INCOMPLETE_FRAME && len == MAX_DEBUG_RECV_SIZE)
              || frameType == WS_ERROR_FRAME) {
            if (frameType == WS_INCOMPLETE_FRAME)
              logger_warning("Message length exceeds limit.");
            else
              logger_warning("Error in incoming frame");
            prepareBuffer;
            frameSize = sprintf((char *)recbuf,
                                "HTTP/1.1 400 Bad Request\r\n"
                                "%s%s\r\n\r\n",
                                versionField,
                                version);
            safeSend(client_fd, recbuf, frameSize);
            goto errout;
          }
          break;
        case WS_STATE_NORMAL:
        case WS_STATE_CLOSING:
          frameType = wsParseInputFrame(recbuf, len, &data, &dataSize);
          if ((frameType == WS_INCOMPLETE_FRAME && len == MAX_DEBUG_RECV_SIZE)
              || frameType == WS_ERROR_FRAME) {
            if (frameType == WS_INCOMPLETE_FRAME)
                logger_warning("Message length exceeds limit.");
            else
              logger_warning("Error in incoming frame");
            prepareBuffer;
            wsMakeFrame(NULL, 0, recbuf, &frameSize, WS_CLOSING_FRAME);
            if (safeSend(client_fd, recbuf, frameSize) == EXIT_FAILURE)
              goto errout;
            state = WS_STATE_CLOSING;
            initNewFrame;
          }
          if (frameType == WS_CLOSING_FRAME) {
            if (state == WS_STATE_CLOSING) {
                goto errout;
            } else {
                prepareBuffer;
                wsMakeFrame(NULL, 0, recbuf, &frameSize, WS_CLOSING_FRAME);
                safeSend(client_fd, recbuf, frameSize);
                goto errout;
            }
          } else if (frameType == WS_TEXT_FRAME) {
            data[ dataSize ] = 0;
            // 此时数据已经解密
            logger_debug("recievedString:%s\n", data);
            char *send_buf = analysis_json_packet(data);
            prepareBuffer;
            if (send_buf) {
              wsMakeFrame(send_buf, strlen(send_buf), recbuf, &frameSize, WS_TEXT_FRAME);
            } else {
              char *errorstr = "[{\"status\":\"error\"}]";
              wsMakeFrame(errorstr, strlen(errorstr), recbuf, &frameSize, WS_TEXT_FRAME);
            }
            // 此时数据已经加密
            if (safeSend(client_fd, recbuf, frameSize) == EXIT_FAILURE)
                goto errout;
            initNewFrame;
          }
          break;
        default:
          state = 0;
      }
    } while(!restart);
errout:
    epoll_ctl(epollfd, EPOLL_CTL_DEL, client_fd, &ev);
    close(client_fd);
    logger_info("close client...");
  }

  free_nosafe_outbuf();//base64 非安全调用
  destory_temp_buf();
  free(recbuf);
  logger_info("exit...");
  close(listener);
  return 0;
}

void start_debug_task(void)
{
  pthread_t pd;
  pthread_create(&pd, NULL, debugThread, NULL);
}
