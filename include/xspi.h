/*
 * xspi.h
 *
 *  Created on: Aug 14, 2020
 *      Author: root
 */

#ifndef INCLUDE_XSPI_H_
#define INCLUDE_XSPI_H_

#define BASE_ADDRESS   0x0416b4
#define ED_ADDRESS     BASE_ADDRESS+1024


#define RD_STATUS 1
#define WR_STATUS 2
#define ED_STATUS 3



int SPI10_Configration(void);
int SPItransfer(int fd, uint8_t const *tx, uint8_t const *rx, size_t len);
int SPI_Processing(int fd);


#endif /* INCLUDE_XSPI_H_ */
