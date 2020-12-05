
/* 
 * File:   common.h
 *
 * Created on March 19, 2019, 5:22 PM
 */

#ifndef COMMON_H
#define COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef uintptr_t Handle_t;

#define __PACKED __attribute__((packed, aligned(1)))
#define INVALID_HANDLE (Handle_t)(-1)

#define NELEMS(__x) (sizeof(__x)/sizeof((__x)[0]))

#define container_of(ptr, type, member) ((type *) \
            ((char *)ptr - offsetof(type, member) ))

#define to_end_of(type, member) (sizeof(type) - offsetof(type, member) )

#define MEM_SIZE(type, member) sizeof(((type*)0)->member)

#define UNUSED(__x) (void)(__x)

#define MAX_SERIAL_NUM (4)
#define DB_MAX_COL_NUM (64)

#ifdef __cplusplus
}
#endif

#endif /* COMMON_H */

