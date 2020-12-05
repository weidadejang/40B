#include <stdio.h>
#include <stdlib.h>
#include <linux/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#define GPIOn_IOx(n, x)  ((n-1)*32 + x)
#define LED_DATA GPIOn_IOx(7, 13)
#define LED_CLK  GPIOn_IOx(4, 5)
#define LED_STB  GPIOn_IOx(1, 8)

#define IN1 GPIOn_IOx(5, 17)
#define IN2 GPIOn_IOx(5, 16)
#define IN3 GPIOn_IOx(5, 15)
#define IN4 GPIOn_IOx(5, 14)
#define IN5 GPIOn_IOx(5, 13)
#define IN6 GPIOn_IOx(5, 12)
#define IN7 GPIOn_IOx(5, 11)
#define IN8 GPIOn_IOx(5, 10)
#define IN9 GPIOn_IOx(5, 9)

#define OUT1_0 GPIOn_IOx(5, 8)
#define OUT2_3 GPIOn_IOx(4, 25)
#define OUT3_0 GPIOn_IOx(1, 2)
#define OUT4_0 GPIOn_IOx(1, 5)


#define GPIO_U_IOCTL_BASE 'x'
#define GPIOC_OPS   _IOWR(GPIO_U_IOCTL_BASE,0,int)

#define GPIO_SET(no,state) 	( no | (state << 31))
#define GPIO_GET(val)		(val >> 31)


void gpio_set_value(int fd,int gpio_no,int state)
{
	unsigned long val;
	val = (!!state << 31) | gpio_no;

	if(ioctl(fd,GPIOC_OPS,&val) < 0){
		perror("ioctl");
	}
}

int  gpio_get_value(int fd,int gpio_no)
{
	unsigned long val = gpio_no;
	if(ioctl(fd,GPIOC_OPS,&val) < 0){
		perror("ioctl");
	}
	return val;
}
int main(int argc,char **argv)
{
	int gpio;
	gpio = open("/dev/gpio",O_RDWR);
	if(gpio < 0){
		perror("open");
		exit(1);
	}

	while(1) {
		printf("IN1=%X\n", IN1);
		printf("IN2=%X\n", IN2);
		printf("IN3=%X\n", IN3);
		printf("IN4=%X\n", IN4);
		printf("IN5=%X\n", IN5);
		printf("IN6=%X\n", IN6);
		printf("IN7=%X\n", IN7);
		printf("IN8=%X\n", IN8);
		printf("IN9=%X\n", IN9);
		sleep(1);
  }
	close(gpio);

	return 0;
}

