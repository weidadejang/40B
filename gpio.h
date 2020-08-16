/*
 * gpio.h
 *
 *  Created on: Jan 29, 2019
 *      Author: root
 */

#ifndef GPIO_H_
#define GPIO_H_

int GPIO_Ctrl(unsigned int Number,unsigned int status);
int GPIO_DEV_CON(const char *devname, unsigned char V);

#define GPIO_STB     "/sys/class/leds/usr0/brightness"
#define GPIO_CLK     "/sys/class/leds/usr3/brightness"
#define GPIO_DATA    "/sys/class/leds/usr2/brightness"

#define GPIODATA_CON(V) GPIO_DEV_CON(GPIO_DATA, V)
#define GPIOCLK_CON(V)  GPIO_DEV_CON(GPIO_CLK, V)
#define GPIOSTB_CON(V) GPIO_DEV_CON(GPIO_STB, V)

void Signal_2led(void);

#define setbit(x,y)  (x) |= (1 << (y))
#define clrbit(x,y)  (x) &= ~(1 << (y))

/*
#define  TM1618_STB_LO   GPIOSTB_CON(1)
#define  TM1618_STB_HI   GPIOSTB_CON(0)

#define  TM1618_DATA_LO  GPIODATA_CON(1)
#define  TM1618_DATA_HI  GPIODATA_CON(0)

#define  TM1618_CLK_LO   GPIOCLK_CON(1)
#define  TM1618_CLK_HI   GPIOCLK_CON(0)
*/


#define  TM1618_STB_LO   GPIO_Ctrl(0,1)
#define  TM1618_STB_HI   GPIO_Ctrl(0,0)

#define  TM1618_DATA_LO  GPIO_Ctrl(2,1)
#define  TM1618_DATA_HI  GPIO_Ctrl(2,0)

#define  TM1618_CLK_LO   GPIO_Ctrl(3,1)
#define  TM1618_CLK_HI   GPIO_Ctrl(3,0)


#define  TM16_STB_Low()    TM1618_STB_LO
#define  TM16_STB_High()   TM1618_STB_HI

#define  TM16_DIO_Low()    TM1618_DATA_LO
#define  TM16_DIO_High()   TM1618_DATA_HI

#define  TM16_CLK_Low()    TM1618_CLK_LO
#define  TM16_CLK_High()   TM1618_CLK_HI



#endif /* GPIO_H_ */
