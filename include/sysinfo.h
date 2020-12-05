
#ifndef __SYSINFO__
#define __SYSINFO__

#include <stdint.h>

typedef struct SysTime {
  uint8_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} __attribute__((packed, aligned(1))) SysTime;


void print_sysinfo(void);

void get_sys_time(SysTime *tm);
char *sys_time_string(char *stime, size_t size);
void Get_SysTime(void);
void set_sys_time(SysTime tm);

void msleep(int ms);
void myusleep(int usec);
#endif
