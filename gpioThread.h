
#ifndef __GPIOTHREAD__
#define __GPIOTHREAD__

/*
 * LED 三个控制管脚：
 * LED1:DATA->OUT4_1->JP6:GPIO_18   GPIO7_IO13
 * LED2:CLK ->OUT4_1->JP6:GPIO_19   GPIO4_IO05
 * LED3:STB ->OUT4_1->JP6:GPIO_8    GPIO1_IO08
 *
 * 拨码开关及复位按键
 * IN9 :DISP0_DAT15                 GPIO5_IO09
 * IN8 :DISP0_DAT16                 GPIO5_IO10
 * IN7 :DISP0_DAT17                 GPIO5_IO11
 * IN6 :DISP0_DAT18                 GPIO5_IO12
 * IN5 :DISP0_DAT19                 GPIO5_IO13
 * IN4 :DISP0_DAT20                 GPIO5_IO14
 * IN3 :DISP0_DAT21                 GPIO5_IO15
 * IN2 :DISP0_DAT22                 GPIO5_IO16
 * IN1 :DISP0_DAT23                 GPIO5_IO17
 *
 * SPI 芯片四个复位脚
 * OUT1_0: DISP0_DAT14              GPIO5_IO08
 * OUT2_3: DISP0_DAT04              GPIO4_IO25
 * OUT3_0: GPIO_2                   GPIO1_IO02
 * OUT4_0: GPIO_5                   GPIO1_IO05
 * */

#define GPIOn_IOx(n, x)  ((n-1)*32 + x)

#define LED_DATA GPIOn_IOx(5, 17)
#define LED_CLK  GPIOn_IOx(5, 16)
#define LED_STB  GPIOn_IOx(5, 15)

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
#define OUT2_3 GPIOn_IOx(4, 25)  //CHIP U2 GPIO4_25
#define OUT3_0 GPIOn_IOx(1, 2)
#define OUT4_0 GPIOn_IOx(1, 5)


#define U2_RESET  	GPIOn_IOx(4, 25)  //CHIP U2   OUT2_3  GPIO4_25
#define U14_RESET 	GPIOn_IOx(4, 28)  //CHIP U14  OUT2_0  GPIO4_28
#define U21_RESET 	GPIOn_IOx(5, 6)   //CHIP U21  OUT1_2  GPIO5_6     spidev1.0
#define U25_RESET 	GPIOn_IOx(7, 12)  //CHIP U25  OUT3_3  GPIO7_12    spidev0.0


#define setbit(x,y)  (x) |= (1 << (y))
#define clrbit(x,y)  (x) &= ~(1 << (y))

#define  TM16_STB_Low()    gpio_set_value(LED_STB, 1)
#define  TM16_STB_High()   gpio_set_value(LED_STB, 0)

#define  TM16_DIO_Low()    gpio_set_value(LED_DATA, 1)
#define  TM16_DIO_High()   gpio_set_value(LED_DATA, 0)

#define  TM16_CLK_Low()    gpio_set_value(LED_CLK, 1)
#define  TM16_CLK_High()   gpio_set_value(LED_CLK, 0)


int gpio_set_value(int pin, char value);
int gpio_get_vale(int pin);

void Signal_2led(void);

void start_gpio_task(void);

#endif /* GPIO_H_ */
