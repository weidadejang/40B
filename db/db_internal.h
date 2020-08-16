/*
 * File:   db_internal.h
 *
 * Created on March 15, 2019, 1:24 PM
 */

#ifndef DB_INTERNAL_H
#define DB_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "db.h"

enum ElemType { ET_INT, ET_BLOB, ET_STRING, ET_NUM };

struct Elem {
  const char *key;
  enum ElemType type;
  const int prim;
  size_t addr;
  const char *defval;
};

struct Table {
  const char *name;            // table name
  const size_t struct_size;    // sizeof(struct table)
  struct Elem elems[DB_MAX_COL_NUM];
};

struct DbDriver {
  const char *name;
  Handle_t (*open)(const char *);
  void (*close)(Handle_t *);
  int (*create)(Handle_t, const struct Table *);
  int (*update)(Handle_t, const struct Table *, uint8_t *base);
  int (*restore)(Handle_t, const struct Table *, uint8_t *cond,
                 uint8_t *(*get_buffer)(const struct Table *, const void *prim,
                           void *oqaue), void *opaque);
  int (*delete)(Handle_t, const struct Table *, uint8_t *base);
  int (*transction)(Handle_t,
      int (*callback)(Handle_t , const char *table_name),
      Handle_t db_handle, const char *table_name);
  int (*exec_sql)(Handle_t, const char* sqlstr);
};

static inline const char *get_key_type(enum ElemType type) {
  switch (type) {
    case ET_INT:
      return "INTERGER";
    case ET_STRING:
      return "TEXT";
    case ET_BLOB:
      return "BLOB";
    default:
      return "TEXT";
  }
}

static inline char *get_value_string(const uint8_t *base) {
  char *str = *((char **)base);
  if (str)
    return str;

  return NULL;
}

static inline void fill_table_with_default(const struct Table *table, \
                                           uint8_t *base) {
  const struct Elem *elem = table->elems;
  struct Blob *blob;

  for (; elem->key; elem++) {
    if (elem->prim || !elem->defval) continue;
    switch (elem->type) {
      case ET_INT:
        *(int *)(base + elem->addr) = strtol(elem->defval, NULL, 10);
        break;
      case ET_STRING:
        *(char **)(base + elem->addr) = strdup(elem->defval);
        break;
      case ET_BLOB:
        blob = (struct Blob*)(base + elem->addr);
        blob->data = strdup(elem->defval);
        blob->nLen = strlen(elem->defval);
        break;
      default:
        printf("Unknown type %d\n", elem->type);
        break;
    }
  }
}

extern struct DbDriver sqlite;

#ifdef __cplusplus
}
#endif

#endif /* DB_INTERNAL_H */
