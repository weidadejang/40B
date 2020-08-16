#ifndef __SERIAL_THREAD__
#define __SERIAL_THREAD__

int start_serial_tasks(int ndev, const char *devs[]);

void stop_serial_tasks();
void start_SPI10_task(void);

#endif
