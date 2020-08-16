
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "serial.h"

int com_set_speed(int fd, int speed) {
  int i;
  int status;
  struct termios Opt;
  int speed_arr[] = {B115200, B57600, B38400, B19200, B9600,
                     B4800,   B2400,  B1200,  B300};
  int name_arr[] = {115200, 57600, 38400, 19200, 9600, 4800, 2400, 1200, 300};

  tcgetattr(fd, &Opt);
  for (i = 0; i != sizeof(name_arr) / sizeof(name_arr[0]); i++) {
    if (speed == name_arr[i]) {
      tcflush(fd, TCIOFLUSH);
      cfsetispeed(&Opt, speed_arr[i]);
      cfsetospeed(&Opt, speed_arr[i]);
      status = tcsetattr(fd, TCSANOW, &Opt);
      if (status != 0) {
        printf("tcsetattr fd");
        return -1;
      }
    }

    tcflush(fd, TCIOFLUSH);
  }

  return 0;
}

int com_set_parity(int fd, int databits, int stopbits, char parity) {
  struct termios opt;

  if (tcgetattr(fd, &opt) != 0) {
    printf("SetupSerial\n");
    return -1;
  }
  opt.c_cflag &= ~CSIZE;

  switch (databits) {
    case 7:
      opt.c_cflag |= CS7;
      break;
    case 8:
      opt.c_cflag |= CS8;
      break;
    default:
      printf("Unsupported data size\n");
      return -2;
  }

  switch (parity) {
    case 'n':
    case 'N':
      opt.c_cflag &= ~PARENB;
      opt.c_iflag &= ~INPCK;
      break;
    case 'o':
    case 'O':
      opt.c_cflag |= (PARODD | PARENB);
      opt.c_iflag |= INPCK;
      break;
    case 'e':
    case 'E':
      opt.c_cflag |= PARENB;
      opt.c_cflag &= ~PARODD;
      opt.c_iflag |= INPCK;
      break;
    case 'S':
    case 's':
      opt.c_cflag &= ~PARENB;
      opt.c_cflag &= ~CSTOPB;
      break;
    default:
      printf("Unsupported parity\n");
      return -3;
  }

  switch (stopbits) {
    case 1:
      opt.c_cflag &= ~CSTOPB;
      break;

    case 2:
      opt.c_cflag |= CSTOPB;
      break;

    default:
      printf("Unsupported stop bits\n");
      return -4;
  }

  // Set input parity option
  if (parity != 'n' || parity != 'N') {
    opt.c_iflag |= INPCK;
    opt.c_cc[VTIME] = 1;  // 15 seconds
    opt.c_cc[VMIN] = 0;
  }

  tcflush(fd, TCIFLUSH);  // Update the opt and do it NOW
  if (tcsetattr(fd, TCSANOW, &opt) != 0) {
    printf("Setup Serial stopbits, parity\n");
    return -5;
  }

  return 0;
}

int com_set_mode(int fd, unsigned char dely_time, unsigned char ch_len) {
  struct termios opt;

  tcgetattr(fd, &opt);
  opt.c_lflag &= ~(ICANON | ISIG | ECHO);
  opt.c_iflag &= ~(ICRNL | IGNCR | IXON);
  opt.c_oflag &= ~OPOST;
  opt.c_cc[VTIME] = dely_time;
  opt.c_cc[VMIN] = ch_len;
  tcflush(fd, TCIFLUSH);
  if (tcsetattr(fd, TCSANOW, &opt) != 0) {
    printf("setup the mode of serial\n");
    return -1;
  }

  return 0;
}

int getsetings(const char *settings, SERIAL *comset) {
  char seps[] = ",";
  char *token = NULL;
  char *savestrp;
  char *tt;
  unsigned char i = 0;
  char settmp[14];  // settings ���Ȳ����ܳ�����ô��
  char *substr[4] = {"\0", "\0", "\0", "\0"};

  if (!settings || settings[0] == '\0') return -1;

  if (strlen(settings) > sizeof(settmp) - 1) return -2;

  strcpy(settmp, settings);

  for (i = 0, tt = settmp; i < 4; tt = NULL, i++) {
    token = strtok_r(tt, seps, &savestrp);
    if (!token) break;
    if (!strcmp("\0", token)) return -4;
    substr[i] = token;
  }
  if (i != 4) return -3;

  comset->speed = atoi(substr[0]);
  comset->databits = atoi(substr[1]);
  comset->stopbits = atoi(substr[2]);
  comset->parity = substr[3][0];

  return 0;
}

FDSET serial_init(const char *dev, char *settings, unsigned char dely_time,
                  unsigned char ch_len) {
  FDSET fds;

  fds.epollfd = -1;
  fds.comfd = -1;
  if (!dev || !settings) {
    printf("dev or settings is null.\n");
    return fds;
  }

  fds.comfd = open(dev, O_RDWR | O_NOCTTY);
  if (-1 == fds.comfd) {
    perror("serial");
    return fds;
  }

  SERIAL serial;

  if (0 > getsetings(settings, &serial)) return fds;
  if (0 != com_set_speed(fds.comfd, serial.speed)) return fds;
  if (0 != com_set_parity(fds.comfd, serial.databits, serial.stopbits,
                          serial.parity))
    return fds;
  if (0 != com_set_mode(fds.comfd, dely_time, ch_len)) return fds;

  fds.epollfd = epoll_create(1);
  if (fds.epollfd < 0) {
    perror("epoll_create");
    close(fds.comfd);
    fds.comfd = -1;
    return fds;
  }
  struct epoll_event ev;
  ev.data.fd = fds.comfd;
  ev.events = EPOLLIN;
  if (epoll_ctl(fds.epollfd, EPOLL_CTL_ADD, fds.comfd, &ev) < 0) {
    close(fds.comfd);
    close(fds.epollfd);
    fds.epollfd = -1;
    fds.comfd = -1;
    return fds;
  }

  return fds;
}

void close_serial(FDSET *fds) {
  close(fds->comfd);
  close(fds->epollfd);
  fds->epollfd = -1;
  fds->comfd = -1;
}

int SerialSend(const int fd, const char *buf, int len) {
  int cnt = 0x00, ret, _len = len;
  tcflush(fd, TCIFLUSH);

  while (_len > 0) {
    ret = write(fd, buf + cnt, _len);
    if (ret < 0) {
      return ret;
    }
    cnt += ret;
    _len -= ret;
    if (_len > 0x00) {
      usleep(10000);
    }
  }
  return (cnt);
}
int SerialRec(FDSET fds, unsigned char *buf, int size, int timeout) {
  int nfds, readlen = 0;
  int ret;
  struct epoll_event events[1];

  do {
    nfds = epoll_wait(fds.epollfd, events, 1, timeout);
    if (nfds < 0) {
      if (errno != EINTR) break;
    } else if (nfds) {
      ret = read(fds.comfd, buf + readlen, size - readlen);
      if (ret > 0) {
        readlen += ret;
        if (readlen >= size) return size;
      } else {
        if (errno != EINTR) {
          break;
        }
      }
    } else {
      return readlen;
    }
  } while (1);

  return -1;
}
int SerialGetLine(FDSET fds, unsigned char *buf, int size, int timeout) {
  int pc = 0;
  while (pc < size) {
    if (SerialRec(fds, buf + pc, 1, timeout) > 0) {
      if (buf[pc] == '\r' || buf[pc] == '\n') return pc++;
      pc++;
    } else {
      return pc ? pc++ : -1;
    }
  }
  return pc;
}
