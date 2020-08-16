#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/sysinfo.h>

#include "log.h"

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
