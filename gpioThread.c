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
#include "gpioThread.h"

/* Private consts ------------------------------------------------------------*/
const unsigned char TM16_SevenSegmentTbl[10][2] = {	//idx  S1 S2 S3 S4 S5 XX XX XX                 XX XX XX S6 S7 S8 XX XX
  //      a   b  c  d  e                                    f   g   dp
  { 0x1f, 0x08 },		//       1  1   1   1  1   0    0   0         0    0   0   1   0    0   0   0        ->' 0 '
  { 0x06, 0x00 },		//       0  1   1   0  0   0    0   0         0    0   0   0   0    0   0   0        ->' 1 '
  { 0x1b, 0x10 },		//       1  1   0   1  1   0    0   0         0    0   0   0   1    0   0   0        ->' 2 '
  { 0x0f, 0x10 },		//       1  1   1   1  0   0    0   0         0    0   0   0   1    0   0   0        ->' 3 '
  { 0x06, 0x18 },		//       0  1   1   0  0   0    0   0         0    0   0   1   1    0   0   0        ->' 4 '
  { 0x0d, 0x18 },		//       1  0   1   1  0   0    0   0         0    0   0   1   1    0   0   0        ->' 5 '
  { 0x1d, 0x18 },		//       1  0   1   1  1   0    0   0         0    0   0   1   1    0   0   0        ->' 6 '
  { 0x07, 0x00 },		//       1  1   1   0  0   0    0   0         0    0   0   0   0    0   0   0        ->'7 '
  { 0x1f, 0x18 },		//       1  1   1   1  1   0    0   0         0    0   0   1   1    0   0   0        ->'8 '
  { 0x0f, 0x18 },		//       1  1   1   1  0   0    0   0         0    0   0   1   1    0   0   0        ->'9 '
};

#define GPIO_ERROR(fd, s)  if (fd < 0) { \
    logger_error("open %s error!", s); \
    return -1; \
  } \

int gpio_get_vale(int pin) {
  int fd;
  char buf[64] = {0};

  fd = open("/sys/class/gpio/export", O_WRONLY);
  GPIO_ERROR(fd, "/sys/class/gpio/export")
  snprintf(buf, sizeof(buf), "%d", pin);
  write(fd, buf, strlen(buf));
  close(fd);

  snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/direction", pin);
  fd = open(buf, O_WRONLY);
  GPIO_ERROR(fd, buf)
	write(fd, "in", 3);
	close(fd);

  snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/value", pin);
	fd = open(buf, O_RDONLY);
  GPIO_ERROR(fd, buf)
  buf[0] = buf[1] = 0;
	read(fd, buf, 2);
  close(fd);

  if (buf[0] == '0') return 0;

  return 1;
}

int gpio_set_value(int pin, char value) {
  int fd;
  char buf[64] = {0};

  fd = open("/sys/class/gpio/export", O_WRONLY);
  GPIO_ERROR(fd, "/sys/class/gpio/export")
  snprintf(buf, sizeof(buf), "%d", pin);
  write(fd, buf, strlen(buf));
  close(fd);

  snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/direction", pin);
  fd = open(buf, O_WRONLY);
  GPIO_ERROR(fd, buf)
	write(fd, "out", 4);
	close(fd);

  snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/value", pin);
	fd = open(buf, O_RDWR);
  GPIO_ERROR(fd, buf)
	write(fd, value?"1":"0", 2);
  close(fd);
  return 0;
}

void TM16_SendOneByte(unsigned char tx_dat)
{
  unsigned char i;
  for (i = 0; i < 8; i++) {
    TM16_CLK_Low();
    SLEEP(0, 100000);
    if (tx_dat & 0x01)
      TM16_DIO_High();
    else
      TM16_DIO_Low();
    SLEEP(0, 100000);
    TM16_CLK_High();
    SLEEP(0, 100000);
    tx_dat >>= 1;
  }
}

void TM16_SendCommand(unsigned char tx_cmd)
{
  unsigned char i;
  TM16_STB_Low();
  SLEEP(0, 100000);
  for (i = 0; i < 8; i++) {
    TM16_CLK_Low();
    SLEEP(0, 100000);
    if (tx_cmd & 0x01)
      TM16_DIO_High();
    else
      TM16_DIO_Low();
    SLEEP(0, 100000);
    TM16_CLK_High();
    SLEEP(0, 100000);
    tx_cmd >>= 1;
  }
  SLEEP(0, 100000);
  TM16_STB_High();
  //SLEEP(0, 100000);
}



void TM16_UpdateOneDigit(unsigned char position, unsigned char value)
{
  TM16_SendCommand(0x44);	//setup data mode (¹Ì¶¨µØÖ·)
  TM16_STB_Low();
  TM16_SendOneByte((position << 1) + 0xC0);	//setup display address
  TM16_SendOneByte(TM16_SevenSegmentTbl[value][0]);
  TM16_STB_High();
  SLEEP(0, 10000);
  TM16_STB_Low();
  TM16_SendOneByte((position << 1) + 0xC1);	//setup display address
  TM16_SendOneByte(TM16_SevenSegmentTbl[value][1]);
  TM16_STB_High();
  SLEEP(0, 10000);
}

void Signal_2led(void)
{
  TM16_SendCommand(0x00);	//setup display mode(4X8)
  TM16_SendCommand(0x44);	//setup data mode (¹Ì¶¨µØÖ·)
  TM16_STB_Low();
  TM16_SendOneByte(0xC4);	//setup display address
  TM16_SendOneByte(0xff);
  TM16_STB_High();

  //SLEEP(0, 10000);
  usleep(100000);

  TM16_STB_Low();
  TM16_SendOneByte(0xC5);	//setup display address
  TM16_SendOneByte(0xff);
  TM16_STB_High();
  //SLEEP(0, 10000);
  usleep(100000);
  TM16_SendCommand(0x88);	//display control command(11/16)
}

void led_autocheck(void)
{
  int i = 0;

  for (i = 0; i < 4; i++) {
    TM16_SendCommand(0x00);	//setup display mode(4X8)
    //sleep(1);
    TM16_UpdateOneDigit(0x00, i);
    //sleep(1);
    TM16_SendCommand(0x88);	//display control command(11/16)

    //sleep(1);
    TM16_SendCommand(0x00);	//setup display mode(4X8)
    //sleep(1);
    TM16_UpdateOneDigit(0x01, i);
    //sleep(1);
    TM16_SendCommand(0x88);	//display control command(11/16)

    sleep(1);
  }
  // TODO: 此处应该关闭led显示
}

uint8_t get_key_value(void) {
  uint8_t key = 0;
  key |= (0x01 & gpio_get_vale(IN1)) << 0;
  key |= (0x01 & gpio_get_vale(IN2)) << 1;
  key |= (0x01 & gpio_get_vale(IN3)) << 2;
  key |= (0x01 & gpio_get_vale(IN4)) << 3;
  key |= (0x01 & gpio_get_vale(IN5)) << 4;
  key |= (0x01 & gpio_get_vale(IN6)) << 5;
  key |= (0x01 & gpio_get_vale(IN7)) << 6;
  key |= (0x01 & gpio_get_vale(IN8)) << 7;

  return key;
}

uint8_t get_reset_key(void) {
  return (0x01 & gpio_get_vale(IN9));
}

void reset_spidev(void)
{
  logger_info("-----spidev reset------");
  gpio_set_value(U2_RESET, 1);
  gpio_set_value(U14_RESET, 1);
  gpio_set_value(U21_RESET, 1);
  gpio_set_value(U25_RESET, 1);
  usleep(200000);
  gpio_set_value(U2_RESET, 0);
  gpio_set_value(U14_RESET, 0);
  gpio_set_value(U21_RESET, 0);
  gpio_set_value(U25_RESET, 0);
  usleep(200000);
  gpio_set_value(U2_RESET, 1);
  gpio_set_value(U14_RESET, 1);
  gpio_set_value(U21_RESET, 1);
  gpio_set_value(U25_RESET, 1);
  usleep(500000);
}



void *gpioThread(void *arg)
{
  uint8_t type = 0;
  uint8_t reset = 0;
  int reset_timeout = 0;

  UNUSED(arg);
  pthread_detach(pthread_self());
  pthread_setspecific(tls_key_threadnr, "gpioThread");

  //reset_spidev();
  logger_info("Running led self-checking program.");
//  while(1)
//  {
	  Signal_2led();
	  led_autocheck();
	  //TM16_STB_Low();
	  //TM16_DIO_Low();
	  //TM16_CLK_Low();
	  //TM16_STB_High();
	  //TM16_DIO_High();
	  //TM16_CLK_High();
//	  sleep(1);
//	  printf("this is ok\n");
//  }

  logger_info("running...");

  System *sysconf = get_system_conf();
  sysconf->Type &= 0x000000ff;

  while (!restart) {
    type = get_key_value();
    reset = get_reset_key();
    printf("type = %x reset = %x \n", type, reset);

    if (reset) reset_timeout ++;
    else reset_timeout = 0;

    if (reset_timeout > 10) {
      printf("Lower the reset pin for more than 10 seconds \n");
      // do somthing....
    }

    if (type != sysconf->Type) {
      TM16_UpdateOneDigit(0x00, type & 0x0f);
      TM16_UpdateOneDigit(0x01, type >> 8);
      rwlock_wrlock();
      sysconf->Type = type;
      db_update_row(db_handle, sysconf);
      rwlock_unlock();
    }
    sleep(20);
  }

  logger_info("exit...");

  return 0;
}

void start_gpio_task(void)
{
  pthread_t pd;

  pthread_create(&pd, NULL, gpioThread, NULL);
}
