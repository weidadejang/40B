
#include <log.h>
#include "db.h"
#include "db_internal.h"
#include "list.h"

#define to_DbCacheRow(ptr) container_of(ptr, DbCacheRow, data)

typedef struct DbPriv_t {
  const struct DbDriver *driver;
  Handle_t driver_handle;
  list *cached_table;      //db cache in memory
} DbPriv_t;

typedef struct DbCacheTable {
  const char *name;       // table name
  list *rows;             // db rows in memory
  list *recycle;          // recycle bin
} DbCacheTable;

typedef struct DbCacheRow {
  const struct Table *desc; // table description
  DbCacheTable *parent;
  uint8_t data[0];
} DbCacheRow;

/* All supported db drivers */
static const struct DbDriver *drivers[] = {&sqlite, NULL};

/* DB description in form of {struct Table} */
static const struct Table db_tables[] = {
#define INT ET_INT
#define TEXT ET_STRING
#define BLOB ET_BLOB
#define TABLEBEGIN(__table)                                     \
{                                                               \
  .name = #__table,                                             \
  .struct_size = sizeof(struct __table),                        \
  .elems = {
#define ELEM_DEFAULT(__table, __name, __type, __prim, __defval) \
    {                                                           \
      .key = #__name,                                           \
      .type = __type,                                           \
      .prim = __prim,                                           \
      .addr = offsetof(struct __table, __name),                 \
      .defval = __defval,                                       \
    },
#define TABLEEND(__table)                                       \
  }/*end elems*/                                                \
}, /*end table*/
#define ELEM(__table, __name, __type, __prim) \
        ELEM_DEFAULT(__table, __name, __type, __prim, NULL)

#include "dbtable.def"

#undef TABLEBEGIN
#undef TABLEEND
#undef ELEM_DEFAULT
#undef ELEM
#undef INT
#undef BLOB
#undef TEXT
};

/* Local function definition */
static const struct Table *get_desc_by_name(const char *table_name);
static int cache_match_table(void *data, void *table_name);
static void cache_free_table(void *data);
static DbCacheTable *cache_get_table(DbPriv_t *priv, const char *name);
static int cache_match_row(void *data, void *prim_val);
static void cache_free_row(void *data);
static uint8_t *cache_get_row(const struct Table *table, const void *prim_val,
                              void *opaque);

/* External functions */
Handle_t db_open(const char *name) {
  DbPriv_t *priv = malloc(sizeof(*priv));
  if (!priv) return INVALID_HANDLE;

  priv->driver = drivers[0];
  priv->driver_handle = priv->driver->open(name);
  priv->cached_table = listCreate();
  priv->cached_table->match = cache_match_table;
  priv->cached_table->free = cache_free_table;

  return (Handle_t)priv;
}

void db_close(Handle_t *handle) {
  DbPriv_t *priv = (struct DbPriv_t *)(*handle);

  if (priv->cached_table) listRelease(priv->cached_table);

  if (priv->driver_handle != INVALID_HANDLE)
    priv->driver->close(&priv->driver_handle);

  free(priv);
  *handle = 0;
}

int db_create_table(Handle_t handle, const char *db_name) {
  const struct Table *table = get_desc_by_name(db_name);
  DbPriv_t *priv = (DbPriv_t *)handle;
  int ret = 0;

  if (!table) {
    logger_info("Requested table '%s' is NOT defined!!!", db_name);
    return -EINVAL;
  }

  ret = priv->driver->create(priv->driver_handle, table);
  if (ret < 0) {
    logger_info("Fail to create table for '%s'.", db_name);
    return ret;
  }

  return priv->driver->restore(priv->driver_handle, table, NULL, cache_get_row,
                               priv);
}

static int _cached_sync(Handle_t handle, const char *table_name) {
  DbPriv_t *priv = (DbPriv_t *)handle;
  DbCacheTable *table = cache_get_table(priv, table_name);
  listNode *node;

  if (!table) {
    //logger_info("Table %s is not defined in dbtable.def.", table_name);
    return -EINVAL;
  }

  logger_info("Begin to sync %s with db file...", table_name);
  if (table->recycle->len > 0) {
    // int index = 0;
    // float percentage = 0.0;
    // int total = table->recycle->len;

    for (node = table->recycle->head; node; node = node->next) {
      DbCacheRow *row = node->value;
      if (0 != db_delete_row(handle, row->data)) {
        //logger_info("delete row failed! total: %d, current: %d", total, index);
        return -EINVAL;
      }
      // percentage = ++index / (total * 1.0);
      // logger_info("Delete total: %d  %.2f %%", total, percentage * 100);
    }
    listEmpty(table->recycle);
  }
  if (table->rows->len > 0) {
    // int index = 0;
    // float percentage = 0.0;
    // int total = table->rows->len;

    for (node = table->rows->head; node; node = node->next) {
      DbCacheRow *row = node->value;
      if (0 != db_update_row(handle, row->data)) {
        //logger_info("update row failed! total: %d, current: %d", total, index);
        return -EINVAL;
      }
      // percentage = ++index / (total * 1.0);
      // logger_info("Update total: %d  %.2f %%", total, percentage * 100);
    }
  }

  return 0;
}

int db_cached_sync(Handle_t handle, const char *table_name) {
  DbPriv_t *priv = (DbPriv_t *)handle;

  return priv->driver->transction(priv->driver_handle, _cached_sync, handle, table_name);
}

int db_restore_row(Handle_t handle, void *pRow) {
  DbPriv_t *priv = (DbPriv_t *)handle;
  DbCacheRow *row = to_DbCacheRow(pRow);

  return priv->driver->restore(priv->driver_handle, row->desc, pRow, NULL, NULL);
}

int db_update_row(Handle_t handle, void *pRow) {
  DbPriv_t *priv = (DbPriv_t *)handle;
  DbCacheRow *row = to_DbCacheRow(pRow);

  return priv->driver->update(priv->driver_handle, row->desc, pRow);
}

int db_delete_row(Handle_t handle, void *pRow) {
  DbPriv_t *priv = (DbPriv_t *)handle;
  DbCacheRow *row = to_DbCacheRow(pRow);

  return priv->driver->delete(priv->driver_handle, row->desc, pRow);
}

void *db_cached_get_row(Handle_t handle, const char *table_name, void *prim_val) {
  DbPriv_t *priv = (DbPriv_t *)handle;
  DbCacheTable *table = cache_get_table(priv, table_name);
  DbCacheRow *row;
  listNode *node;

  if (!table) {
    // logger_info("Table %s is not defined in dbtable.def.", table_name);
    return NULL;
  }

  node = listSearchKey(table->rows, prim_val);
  if (!node) return NULL;
  row = node->value;

  return row->data;
}

int db_cached_insert_row(Handle_t handle, void *pRow) {
  DbPriv_t *priv = (DbPriv_t *)handle;
  DbCacheRow *row = to_DbCacheRow(pRow);
  DbCacheTable *table = cache_get_table(priv, row->desc->name);
  if (!table) {
    return 0;
  }

  row->parent = table;
  listAddNodeTail(table->rows, row);

  return 0;
}

void db_cached_delete_row(Handle_t handle, void *pRow) {
  DbPriv_t *priv = (DbPriv_t *)handle;
  DbCacheRow *row = to_DbCacheRow(pRow);
  const struct Elem *elem = row->desc->elems;
  DbCacheTable *table = row->parent;
  listNode *node;

  UNUSED(priv);

  if (!table) {
    cache_free_row(pRow);
    return;
  }

  for (; elem->key; elem++)
    if (elem->prim) break;
  node = listSearchKey(table->rows, row->data + elem->addr);
  if (!node) return;

  listRemoveNode(table->rows, node);
  listAddNodeTail(table->recycle, row);
}

int db_cached_clear_table(Handle_t handle, const char *table_name) {
  DbPriv_t *priv = (DbPriv_t *)handle;
  DbCacheTable *table = cache_get_table(priv, table_name);
  int count = 0;

  if (!table) return 0;

  listNode *node = table->rows->head;
  if (!node) return 0;

  count = table->rows->len;

  listJoin(table->recycle, table->rows);

  return count;
}

int db_cached_traverse_row(Handle_t handle, const char *table_name,
                           void *prim_val, trav_cb cb, void *opaque) {
  DbPriv_t *priv = (DbPriv_t *)handle;
  DbCacheTable *table = cache_get_table(priv, table_name);
  listNode *node = NULL;
  int count = 0;

  if (!table) return count;

  if (prim_val)
    node = listSearchKey(table->rows, prim_val);

  if (!node) node = table->rows->head;

  for (; node; node = node->next, count++) {
    DbCacheRow *row = node->value;
    if (cb(opaque, row->data) != 0)
      break;
  }
  // 遍历到list尾部时，键值自动清零，保证自动从头开始
  if (!node && prim_val) *((uint32_t*)prim_val) = 0;

  //printf("==========================================================>Get Number of Task %d \n",count);

  return count;
}

void *db_alloc_row(const char *table_name) {
  const struct Table *table = get_desc_by_name(table_name);
  DbCacheRow *row;

  if (!table) {
    logger_info("Table %s is not defined in dbtable.def.", table_name);
    return NULL;
  }

  row = calloc(1, sizeof(*row) + table->struct_size);
  if (!row) return NULL;
  row->desc = table;
  row->parent = NULL;
  fill_table_with_default(row->desc, row->data);

  return row->data;
}

void db_free_row(void *pRow) {
  DbCacheRow *row = to_DbCacheRow(pRow);
  cache_free_row(row);
}

void db_dump_row(const void *pRow) {
  DbCacheRow *row = to_DbCacheRow(pRow);
  const struct Elem *elem = row->desc->elems;
  const char *str;
  const struct Blob *blob;

  logger_info("Table %s :", row->desc->name);

  for (; elem->key; elem++) {
    logger_info("%s : ", elem->key);
    switch (elem->type) {
      case ET_INT:
        logger_info("%u", *(int*)(row->data + elem->addr));
        break;
      case ET_STRING:
        str = get_value_string(row->data + elem->addr);
        logger_info("%s", str ? str : "nullptr");
        break;
      case ET_BLOB:
        blob = (struct Blob *)(row->data + elem->addr);
        str = blob->data;
        logger_hexbuf("BLOB:", blob->data, blob->nLen);
        // logger_info("%u", blob->nLen);
        // for(int i = 0; i < blob->nLen && i < 80; i++) logger_info("%02X ",str[i]);
        // logger_info("");
        break;
      default:
        break;
    }
  }
}

/* Local function definition */
const struct Table *get_desc_by_name(const char *table_name) {
  unsigned int idx;
  for(idx = 0; idx < NELEMS(db_tables); idx++)
    if (!strcmp(db_tables[idx].name, table_name))
      return db_tables + idx;
  return NULL;
}

int cache_match_table(void *data, void *table_name) {
  const char *expected = table_name;
  const DbCacheTable *cache = data;

  return !strcmp(cache->name, expected);
}

void cache_free_table(void *data) {
  DbCacheTable *table = data;

  logger_info("Release cache table: %s", table->name);
  listRelease(table->rows);
  listRelease(table->recycle);
  free(table);
}

DbCacheTable *cache_get_table(DbPriv_t *priv, const char *name) {
  const struct Table *desc = get_desc_by_name(name);
  DbCacheTable *table;
  listNode *node;

  if (!desc) return NULL;

  /* Check if cache-table is already created */
  node = listSearchKey(priv->cached_table, (void*)name);
  if (!node) {
    table = malloc(sizeof(*table));
    if (!table) return NULL;
    table->name = name;
    table->rows = listCreate();
    table->rows->free = cache_free_row;
    table->rows->match = cache_match_row;
    table->recycle = listCreate();
    table->recycle->free = cache_free_row;
    table->recycle->match = cache_match_row;

    listAddNodeTail(priv->cached_table, table);
    logger_info("Create new cache table: %s", name);
  } else
    table = node->value;

  return table;
}

int cache_match_row(void *data, void *prim_val) {
  const DbCacheRow *row = data;
  const struct Elem *elem = row->desc->elems;

  for (; elem->key; elem++)
    if (elem->prim) {
      const char *tmpstr = get_value_string(row->data + elem->addr);
      const int *tmpint = (int *)(row->data + elem->addr);
      switch (elem->type) {
        case ET_INT:
          return *tmpint == *(int *)prim_val;
        case ET_STRING:
          return !strcmp(tmpstr, prim_val);
        default: break;
      }
    }

  return 0;
}

void cache_free_row(void *data) {
  DbCacheRow *row = data;
  const struct Elem *elem = row->desc->elems;
  struct Blob *blob;
  char *tmpstr;

  for (; elem->key; elem++) {
    switch (elem->type) {
      case ET_STRING:
        tmpstr = get_value_string(row->data + elem->addr);
        free(tmpstr);
        break;
      case ET_BLOB:
        blob = (struct Blob *)(row->data + elem->addr);
        free(blob->data);
        break;
      default:
        break;
    }
  }

  free(row);
}

uint8_t *cache_get_row(const struct Table *desc, const void *prim_val,
                       void *opaque) {
  DbPriv_t *priv = opaque;
  DbCacheTable *table = cache_get_table(priv, desc->name);
  DbCacheRow *row;
  listNode *node = NULL;

  if (!table) {
    return NULL;
  }
  /* Check if prim_val already exists in this cache */
  if (prim_val) node = listSearchKey(table->rows, (void*)prim_val);

  if (!node) {
    uint8_t *base = db_alloc_row(desc->name);
    if (!base) return NULL;
    db_cached_insert_row((Handle_t)opaque, base);
    row = to_DbCacheRow(base);
  } else
    row = node->value;

  return row->data;
}

// 用于执行特殊的sql语句，提高db模块的灵活性
int db_exec_sqlstr(Handle_t handle, const char* sqlstr) {
  DbPriv_t *priv = (DbPriv_t *)handle;

  return priv->driver->exec_sql(priv->driver_handle, sqlstr);
}
