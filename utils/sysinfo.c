#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <time.h>
#include <error.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysinfo.h>

#include "sysinfo.h"
#include "log.h"

extern int errno;

const char data_mem[] = "VmRSS:";

unsigned int print_mem(int pid)
{
    FILE *stream;
    char cache[32];
    char mem_info[64];
    unsigned int mem_total = 0;

    sprintf(mem_info, "/proc/%d/status", pid);
    stream = fopen(mem_info, "r");
    if (stream == NULL) {
        return 0;
    }

    while(fscanf(stream, "%s", cache) != EOF) {
        if (strncmp(cache, data_mem, sizeof(data_mem)) == 0) {
            if (fscanf(stream, "%s", cache) != EOF) {
              mem_total = atoi(cache);
              break;
            }
        }
    }
		fclose(stream);

    return mem_total;
}


void print_sysinfo(void)
{
    struct sysinfo info;

    if(!sysinfo(&info)) {
      float unit = info.mem_unit / 1000.0;
      logger_info("Uptime   : %ld", info.uptime);
      logger_info("Procs    : %d", info.procs);
      // logger_info("Loads    : %ld %ld %ld", info.loads[0], info.loads[1], info.loads[2]);
      logger_info("TotalRAM : %ld kB", (unsigned long)(info.totalram * unit));
      logger_info("FreeRAM  : %ld kB", (unsigned long)(info.freeram * unit));
      logger_info("Sharedram: %ld kB", (unsigned long)(info.sharedram * unit));
      logger_info("Bufferram: %ld kB", (unsigned long)(info.bufferram * unit));
      logger_info("Totalswap: %ld KB", (unsigned long)(info.totalswap * unit));
      logger_info("Freeswap : %ld kB", (unsigned long)(info.freeswap * unit));
      logger_info("Totalhigh: %ld kB", (unsigned long)(info.totalhigh * unit));
      logger_info("Freehigh : %ld kB", (unsigned long)(info.freehigh * unit));
    }
    logger_info("Programs use memory: %d kB", print_mem(getpid()));
}


void set_sys_time(SysTime tm)
{
    struct tm tptr;
    struct timeval tv;

    tptr.tm_year = tm.year+100;
    tptr.tm_mon = tm.month - 1;
    tptr.tm_mday = tm.day;
    tptr.tm_hour = tm.hour;
    tptr.tm_min = tm.minute;
    tptr.tm_sec = tm.second;

    tv.tv_sec = mktime(&tptr);
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
}

char *sys_time_string(char *stime, size_t size)
{
  struct tm tm_t;
  time_t t_time;

  t_time = time( NULL );
  localtime_r(&t_time, &tm_t);

  memset(stime, 0, size);
  strftime(stime, size, "%Y-%m-%d %X", &tm_t);
  return stime;
}


void Get_SysTime(void)
{
    struct timeval tv;
    struct timezone tz;
    struct tm *t;

    gettimeofday(&tv, &tz);
    t = localtime(&tv.tv_sec);
    printf("time_now:%d-%d-%d %d:%d:%d.%ld \n", 1900+t->tm_year, 1+t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, tv.tv_usec);
}


//得到本地时间
void get_sys_time(SysTime *tm)
{
    struct tm tm_t;
    time_t tt;

    tt = time( NULL );
    localtime_r( &tt, &tm_t );

    tm->year = tm_t.tm_year + 1900 - 2000;
    tm->month = tm_t.tm_mon + 1;
    tm->day = tm_t.tm_mday;
    tm->hour = tm_t.tm_hour;
    tm->minute = tm_t.tm_min;
    tm->second = tm_t.tm_sec;
}

// 延时ms
void msleep(int ms)
{
	struct timeval delay;
	delay.tv_sec = 0;
	delay.tv_usec = ms * 1000;
	select(0, NULL, NULL, NULL, &delay);
}

//延时useconds 微秒
void myusleep(int usec )
{
    if( usec < 1 || usec > 999999 )
        return ;
    struct timespec delay;
    struct timespec rem;
    memset( &delay, 0, sizeof(delay));
    memset( &rem, 0, sizeof(rem));
    delay.tv_nsec = usec*1000;
    do{
        if( !nanosleep( &delay, &rem ))
        {
            break;
        }
        if( errno == EINTR ) {
            delay.tv_sec = rem.tv_sec;
            delay.tv_nsec = rem.tv_nsec;
        } else {
            break;
        }
    }while(1);
}

#if 0
int main()
{
  char buf[32];
  printf("time:%s\n",sys_time_string(buf, sizeof(buf)));
}
#endif
