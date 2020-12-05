/*
 * @file: wrap_memory.c
 * @author: Lin, Chao
 * @create time: 2020-10-26 13:07:56
 * @last modified: 2020-10-26 13:07:56
 * @description:
 */
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#include "wrap_memory.h"
#include "log.h"

void __real_free(void *);
void __real_free(void *P)
{
	(void)P;
}

void __wrap_free(void *p)
{
  logger_info("-------my_free=%p-----------", p);
  void *bt[20];
  char **strings;
  size_t sz;
  int i;
  sz = backtrace(bt, 20);
  strings = backtrace_symbols(bt, sz);
  for(i = 0; i < sz; ++i)
    logger_info("%s", strings[i]);

  if (p) __real_free(p);
}


#if 0

int main()
{
  char *p = (char*)malloc(20);
  free(p);
}

#endif
