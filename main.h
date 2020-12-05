/*
 * @file: main.h
 * @create time: 2019-01-16 13:58:30
 * @last modified: 2019-01-16 13:58:30
 * @description:
 */

#ifndef __MAIN__
#define __MAIN__

#include <sys/types.h>
#include <signal.h>
#include <assert.h>
#include "automic.h"
#include "wrap_memory.h"

#include <time.h>
#include "db.h"
#include "log.h"
#include "list.h"


#define NET_DEV_NAME "eth0"

/* The macro below defines a version of sleep using nanosleep
 * If a signal such as SIG_CHLD interrupts the sleep we just continue sleeping
 */
#define SLEEP(seconds, nanoseconds)   \
  do {                                \
    struct timespec tv;               \
    tv.tv_sec = (seconds);            \
    tv.tv_nsec = (nanoseconds);       \
    while (nanosleep(&tv, &tv) == -1) \
      ;                               \
  } while (0)
#define CHECK_MALLOC(_ptr, _return)   \
  do {                                \
    if (!_ptr) {                      \
      logger_error("alloc failed!!!"); \
		  kill(getpid(), SIGUSR1);        \
      return (_return);               \
    }                                 \
  } while (0)

#ifdef TEST
#define ASSERT assert
#else
#define ASSERT(__cond) { \
  if (!(__cond)) { \
    logger_debug("check (%s) failed.", #__cond); \
    return -EINVAL; \
  } \
}
#endif


#define MEM_CHECK_RPTR(_ptr)  CHECK_MALLOC(_ptr, NULL)
#define MEM_CHECK_R_1(_ptr)   CHECK_MALLOC(_ptr, -1)
#define MEM_CHECK_R_1(_ptr)   CHECK_MALLOC(_ptr, -1)

extern Handle_t db_handle;
extern int sys_id; // The id of system config, workaround for db.
extern int restart;
extern atomic_t task_update;

#define clear_ed_status() db_cached_clear_table(db_handle, "EDStatus")
#define clear_ed_task() db_cached_clear_table(db_handle, "EDTask")
#define clear_white_list() db_cached_clear_table(db_handle, "WhiteList")
#define get_ed_status(edid) db_cached_get_row(db_handle, "EDStatus", &edid)
#define get_ed_task(edid) db_cached_get_row(db_handle, "EDTask", &edid)
#define get_white_list(edid) db_cached_get_row(db_handle, "WhiteList", &edid)
#define get_system_conf() db_cached_get_row(db_handle, "System", &sys_id)
#define get_extend_table(fun) db_cached_get_row(db_handle, "Extend", &fun)

#define delete_ed_status(status) db_cached_delete_row(db_handle, status)
#define delete_ed_task(task) db_cached_delete_row(db_handle, task)
#define delete_white_list(wl) db_cached_delete_row(db_handle, wl)

#define traverse_ed_status(start, cb, op) db_cached_traverse_row(db_handle, \
            "EDStatus", start, cb, op)
#define traverse_ed_task(start, cb, op) db_cached_traverse_row(db_handle, \
            "EDTask", start, cb, op)
#define traverse_white_list(start, cb, op) db_cached_traverse_row(db_handle, \
            "WhiteList", start, cb, op)

void rwlock_rdlock(void);
void rwlock_wrlock(void);
void rwlock_unlock(void);

#endif
