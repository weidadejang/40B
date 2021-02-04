/*
 * testgpio.c
 *
 *  Created on: Nov 26, 2020
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


int gpio_get_vale(int pin);
int gpio_set_value(int pin, char value);

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


#define GPIOn_IOx(n, x)  ((n-1)*32 + x)

//#define LED_DATA GPIOn_IOx(5, 17)
//#define LED_CLK  GPIOn_IOx(5, 16)
//#define LED_STB  GPIOn_IOx(5, 15)

#define LED_DATA GPIOn_IOx(4, 26)
#define LED_CLK  GPIOn_IOx(4, 27)
#define LED_STB  GPIOn_IOx(4, 29)


#define  TM16_STB_Low()    gpio_set_value(LED_STB, 0)
#define  TM16_STB_High()   gpio_set_value(LED_STB, 1)

#define  STB1    gpio_set_value(LED_STB, 0)
#define  STB0    gpio_set_value(LED_STB, 1)

#define  DIO1    gpio_set_value(LED_DATA, 0)
#define  DIO0    gpio_set_value(LED_DATA, 1)

#define  CLK1    gpio_set_value(LED_CLK, 0)
#define  CLK0    gpio_set_value(LED_CLK, 1)

#define  TM16_DIO_Low()    gpio_set_value(LED_DATA, 0)
#define  TM16_DIO_High()   gpio_set_value(LED_DATA, 1)


#define  TM16_CLK_Low()    gpio_set_value(LED_CLK, 0)
#define  TM16_CLK_High()   gpio_set_value(LED_CLK, 1)


#define GPIO_ERROR(fd, s)  if (fd < 0) { \
    printf("open %s error!", s); \
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

    usleep(10000);
    if (tx_dat & 0x01)
      TM16_DIO_High();
    else
      TM16_DIO_Low();
    usleep(1000);
    TM16_CLK_High();
    usleep(10000);
    tx_dat >>= 1;
  }
}

void TM16_SendCommand(unsigned char tx_cmd)
{
  unsigned char i;
  TM16_STB_Low();
  usleep(10000);
  for (i = 0; i < 8; i++) {
    TM16_CLK_Low();
    usleep(10000);
    if (tx_cmd & 0x01)
      TM16_DIO_High();
    else
      TM16_DIO_Low();
    usleep(10000);
    TM16_CLK_High();
    usleep(1000);
    tx_cmd >>= 1;
  }
  usleep(10000);
  TM16_STB_High();
  usleep(10000);
}

void TM16_UpdateOneDigit(unsigned char position, unsigned char value)
{
  TM16_SendCommand(0x44);	//setup data mode (¹Ì¶¨µØÖ·)
  TM16_STB_Low();
  TM16_SendOneByte((position << 1) + 0xC0);	//setup display address
  TM16_SendOneByte(TM16_SevenSegmentTbl[value][0]);
  TM16_STB_High();
  usleep(1000);
  TM16_STB_Low();
  TM16_SendOneByte((position << 1) + 0xC1);	//setup display address
  TM16_SendOneByte(TM16_SevenSegmentTbl[value][1]);
  TM16_STB_High();
  usleep(1000);
}


//void TM1618_WriteBYTE(unsigned char DATA)           //дһ���ֽ�
//{
//    unsigned char i;
//    for(i=0;i<8;i++)
//    {
//        CLK0;
//        usleep(20000);
//        if(DATA&0X01)
//            DIO1;
//        else
//            DIO0;
//        DATA>>=1;
//        usleep(20000);
//        CLK1;
//        usleep(20000);
//    }
//}
//
//
//
//void Write_command(unsigned char cmd)       //����������
//{
//    STB0;
//    usleep(20000);
//    TM1618_WriteBYTE(cmd);
//    usleep(20000);
//    STB1;
//}
//
//void Write_num(unsigned char add, unsigned char DATA)
//{
//    Write_command(0x44);
//
//    STB0;
//    usleep(20000);
//    TM1618_WriteBYTE(0xc0|add);
//    TM1618_WriteBYTE(DATA&0x1F);//seg1~seg5
//    usleep(20000);
//    STB1;
//    usleep(20000);
//    STB0;
//    TM1618_WriteBYTE(0xc0|add+1);
//    TM1618_WriteBYTE((DATA&0xE0)>>2);//seg12~seg14
//    STB1;
//}
//
//void init_TM1618(void)
//{
//    unsigned char i;
//
//    Write_command(0x01);//5λ8��ģʽ
//    Write_command(0x8f);//����
//    Write_command(0x40);//��ַ�Զ�+1ģʽ
//    usleep(20000);
//    STB0;
//    usleep(20000);
//    TM1618_WriteBYTE(0xc0);
//    for(i=0;i<16;i++)
//        TM1618_WriteBYTE(0x00);
//    usleep(20000);
//    STB1;
//}



void Signal_2led(void)
{
  TM16_SendCommand(0x00);	//setup display mode(4X8)
  TM16_SendCommand(0x44);	//setup data mode (¹Ì¶¨µØÖ·)

  TM16_STB_Low();
  usleep(1000);
  TM16_SendOneByte(0xC4);	//setup display address
  TM16_SendOneByte(0xff);
  usleep(1000);
  TM16_STB_High();

  //SLEEP(0, 10000);
  usleep(1000);

  TM16_STB_Low();
  usleep(1000);
  TM16_SendOneByte(0xC5);	//setup display address
  TM16_SendOneByte(0xff);
  usleep(1000);
  TM16_STB_High();
  //SLEEP(0, 10000);
  usleep(1000);
  TM16_SendCommand(0x88);	//display control command(11/16)
}



void led_autocheck(void)
{
  int i = 0;

  for (i = 1; i < 9; i++) {
    TM16_SendCommand(0x00);	//setup display mode(4X8)

    TM16_UpdateOneDigit(0x00, i);

    TM16_SendCommand(0x88);	//display control command(11/16)

    //usleep(1);

    TM16_SendCommand(0x00);	//setup display mode(4X8)

    TM16_UpdateOneDigit(0x01, i);

    TM16_SendCommand(0x88);	//display control command(11/16)

    //sleep(1);
  }
  // TODO: 此处应该关闭led显示
}


int main(void)
{


	//init_TM1618();

	//Signal_2led();
	//led_autocheck();
//while(1)
//{
	//TM16_STB_Low();
	//TM16_DIO_Low();
	//TM16_CLK_Low();
//}
	//TM16_STB_High();
	//TM16_DIO_High();
	//TM16_CLK_High();

	printf("this is a test\n");

	Signal_2led();

	while(1)
	{

		led_autocheck();

		sleep(1);
	}
	return 0;
}
