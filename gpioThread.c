/*
 * gpio.c
 *
 *  Created on: Jan 28, 2019
 *      Author: root
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/input.h>
#include "log.h"
#include "main.h"
#include "gpio.h"

/* Private consts ------------------------------------------------------------*/
const unsigned char TM16_SevenSegmentTbl[10][2] =
{                              //idx  S1 S2 S3 S4 S5 XX XX XX                 XX XX XX S6 S7 S8 XX XX
                              //      a   b  c  d  e                                    f   g   dp
        {0x1f, 0x08},        //       1  1   1   1  1   0    0   0         0    0   0   1   0    0   0   0        ->' 0 '
        {0x06, 0x00},        //       0  1   1   0  0   0    0   0         0    0   0   0   0    0   0   0        ->' 1 '
        {0x1b, 0x10},        //       1  1   0   1  1   0    0   0         0    0   0   0   1    0   0   0        ->' 2 '
        {0x0f, 0x10},        //       1  1   1   1  0   0    0   0         0    0   0   0   1    0   0   0        ->' 3 '
        {0x06, 0x18},        //       0  1   1   0  0   0    0   0         0    0   0   1   1    0   0   0        ->' 4 '
        {0x0d, 0x18},        //       1  0   1   1  0   0    0   0         0    0   0   1   1    0   0   0        ->' 5 '
        {0x1d, 0x18},        //       1  0   1   1  1   0    0   0         0    0   0   1   1    0   0   0        ->' 6 '
        {0x07, 0x00},        //       1  1   1   0  0   0    0   0         0    0   0   0   0    0   0   0        ->'7 '
        {0x1f, 0x18},        //       1  1   1   1  1   0    0   0         0    0   0   1   1    0   0   0        ->'8 '
        {0x0f, 0x18},        //       1  1   1   1  0   0    0   0         0    0   0   1   1    0   0   0        ->'9 '
};


 /****************************************************************
 * int GPIO_Ctrl(unsigned int Number,unsigned int status)
 ****************************************************************/
int GPIO_Ctrl(unsigned int Number,unsigned int status)
{
	int fb;
  int boolean = status?1:0;

	fb = open("/dev/led", O_RDWR|O_NONBLOCK);
	if (fb < 0) {
		logger_error("open led device fail!");
		return fb;
	}

  if (Number > 3) {
    logger_info("Number invalid. %d", Number);
    return -1;
  }

  ioctl(fb, boolean, Number);
	close(fb);

	return 0;
}

int GPIO_DEV_CON(const char *devname, unsigned char V)
{
  int fd;

  fd = open(devname, O_RDWR|O_NONBLOCK);
  if (fd < 0) {
    logger_error("can not openfile LED GPIO, errcode.");
    return -1;
  }
  int ret = write(fd, V?"0":"1", 1);
  close(fd);
  if (ret < 0) {
    logger_info("write Led GPIO Err");
  }
  return ret;
}


void TM16_SendOneByte(unsigned char tx_dat)
{
  unsigned char i;
  for (i = 0; i < 8; i ++)
  {
    TM16_CLK_Low();
    SLEEP(0, 10000);
    if (tx_dat & 0x01)
      TM16_DIO_High();
    else
      TM16_DIO_Low();
    SLEEP(0,10000);
    TM16_CLK_High();
    SLEEP(0, 10000);
    tx_dat >>= 1;
  }
}

void TM16_SendCommand(unsigned char tx_cmd)
{
  unsigned char i;
  TM16_STB_Low();
  SLEEP(0,10000);
  for(i=0; i<8; i++)
  {
    TM16_CLK_Low();
    SLEEP(0, 10000);
    if (tx_cmd & 0x01)
      TM16_DIO_High();
    else
      TM16_DIO_Low();
    SLEEP(0,10000);
    TM16_CLK_High();
    SLEEP(0, 10000);
    tx_cmd >>= 1;
  }
  SLEEP(0, 10000);
  TM16_STB_High();
}



void TM16_UpdateOneDigit(unsigned char position, unsigned char value)
{
  TM16_SendCommand(0x44);                //setup data mode (¹Ì¶¨µØÖ·)
  TM16_STB_Low();
  TM16_SendOneByte((position << 1) + 0xC0);        //setup display address
  TM16_SendOneByte(TM16_SevenSegmentTbl[value][0]);
  TM16_STB_High();
  SLEEP(0,10000);
  TM16_STB_Low();
  TM16_SendOneByte((position << 1) + 0xC1);        //setup display address
  TM16_SendOneByte(TM16_SevenSegmentTbl[value][1]);
  TM16_STB_High();
  SLEEP(0,10000);
}

void Signal_2led(void)
{
  TM16_SendCommand(0x00);                //setup display mode(4X8)
  TM16_SendCommand(0x44);                //setup data mode (¹Ì¶¨µØÖ·)
  TM16_STB_Low();
  TM16_SendOneByte(0xC4);        //setup display address
  TM16_SendOneByte(0xff);
  TM16_STB_High();
  SLEEP(0,10000);
  TM16_STB_Low();
  TM16_SendOneByte(0xC5);        //setup display address
  TM16_SendOneByte(0xff);
  TM16_STB_High();
  SLEEP(0,10000);
  TM16_SendCommand(0x88);                //display control command(11/16)
}

void led_autocheck(void)
{
	int i = 0;

	for (i = 0; i < 4; i++) {
		TM16_SendCommand(0x00);                //setup display mode(4X8)
		TM16_UpdateOneDigit(0x00,i);
		TM16_SendCommand(0x88);                //display control command(11/16)

		TM16_SendCommand(0x00);                //setup display mode(4X8)
		TM16_UpdateOneDigit(0x01,i);
		TM16_SendCommand(0x88);                //display control command(11/16)

	}
  // TODO: 此处应该关闭led显示
}


void *gpioThread(void *arg)
{
	int keys_fd;
	struct input_event t;
	char *dev;
  int type = -1;

  UNUSED(arg);
  pthread_detach(pthread_self());
  pthread_setspecific (tls_key_threadnr, "gpioThread");

  /* 此处需要改成固定设备名 */
	dev = getenv("KEYPAD_DEV");
  if (!dev || !strcmp(dev, "")) {
    logger_info("con't get keypad dev name!!");
    logger_info("exit...");
    return NULL;
  }
  keys_fd = open(dev, O_RDONLY|O_NONBLOCK);
	if(keys_fd<0)
	{
		logger_info("open Input Key %s device error!", dev);
		return NULL;
	}

  logger_info("Running led self-checking program.");
  Signal_2led();
  led_autocheck();

  logger_info("running...");

  System *sysconf = get_system_conf();
  sysconf->Type &= 0x000000ff;

  while(!restart) {
		if (read(keys_fd, &t, sizeof(t)) == sizeof(t)) {
			if (t.type == EV_KEY && (t.value == 0 || t.value == 1)) {
				logger_info("t.code = %d t.value = %d ", t.code, t.value);
        // 256-261,  0/1
        unsigned int v = (0x1fUL & (t.code - 256));
        if (t.value) { // Released
          clrbit(type, v);
        } else {       // Pressed
          setbit(type, v);
        }

        rwlock_wrlock();
        sysconf->Type = type;
        db_update_row(db_handle, sysconf);
        rwlock_unlock();
        type = -1;
			}
		}
    if (type != sysconf->Type) {
      type = sysconf->Type;
      TM16_UpdateOneDigit(0x00, type / 10);
      TM16_UpdateOneDigit(0x01, type % 10);
    }
    sleep(1);
	}

	close(keys_fd);
  logger_info("exit...");

  return 0;
}

void start_gpio_task(void)
{
  pthread_t pd;

  pthread_create(&pd, NULL, gpioThread, NULL);
}
