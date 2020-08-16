#include <log.h>
#include <errno.h>
#include <string.h>
#include <malloc.h>
#include <sqlite3.h>
#include "db_internal.h"

//extern char *strdup(const char *__s);

static Handle_t sqlite_open(const char *name) {
  sqlite3 *sql = NULL;
  int ret = sqlite3_open(name, &sql);
  if (ret != SQLITE_OK) {
    logger_info("Fail to create db file %s: %s.", name, sqlite3_errmsg(sql));
    return INVALID_HANDLE;
  }

  return (Handle_t)sql;
}

static void sqlite_close(Handle_t *handle) {
  sqlite3 *sql = (sqlite3 *)(*handle);
  int ret = sqlite3_close(sql);
  if (ret != SQLITE_OK)
    logger_info("Warning: fail to close db: %s.", sqlite3_errmsg(sql));
}

static int sqlite_create(Handle_t handle, const struct Table *table) {
  sqlite3 *sql = (sqlite3 *)handle;
  const struct Elem *elem = table->elems;
  char sqlstr[1024] = "\0";
  char *errmsg = NULL;
  int ret;

  sprintf(sqlstr, "CREATE TABLE IF NOT EXISTS %s (", table->name);
  for (; elem->key; elem++)
    sprintf(sqlstr + strlen(sqlstr), "%s %s DEFAULT 0 %s, ", elem->key,
            get_key_type(elem->type), elem->prim ? "PRIMARY KEY" : "");
  sqlstr[strlen(sqlstr) - 2] = ')';
  strcat(sqlstr, ";");

  // logger_info("str = %s", sqlstr);

  ret = sqlite3_exec(sql, sqlstr, NULL, NULL, &errmsg);
  if (ret != 0) logger_info("Fail to exec sql: %s", errmsg);
  sqlite3_free(errmsg);

  return ret;
}

static int sqlite_update(Handle_t handle, const struct Table *table, uint8_t *base) {
  sqlite3 *sql = (sqlite3 *)handle;
  const struct Elem *elem = table->elems;
  char sqlstr[1024] = "\0";
  const char *tmpstr;
  sqlite3_stmt *stmt = NULL;
  struct Blob *blobs[128] = {NULL};
  int blobnum = 0;
  int ret;

  sprintf(sqlstr, "INSERT OR REPLACE INTO %s VALUES(", table->name);

  for (; elem->key; elem++) {
    switch (elem->type) {
      case ET_INT:
        sprintf(sqlstr + strlen(sqlstr), "%u, ", *(int *)(base + elem->addr));
        break;
      case ET_STRING:
        tmpstr = get_value_string(base + elem->addr);
        if (tmpstr)
          sprintf(sqlstr + strlen(sqlstr), "'%s', ", tmpstr);
        else
          strcat(sqlstr, "NULL, ");
        break;
      case ET_BLOB:
        sprintf(sqlstr + strlen(sqlstr), "?, ");
        blobs[blobnum++] = (struct Blob *)(base + elem->addr);
        break;
      default:
        logger_info("Unknown type %d", elem->type);
        break;
    }
  }
  sqlstr[strlen(sqlstr) - 2] = ')';
  strcat(sqlstr, ";");
  // logger_info("str = %s", sqlstr);

  ret = sqlite3_prepare_v2(sql, sqlstr, strlen(sqlstr), &stmt, NULL);
  if (ret != SQLITE_OK || !stmt) {
    logger_info("Fail to prepare sql: %s", sqlite3_errmsg(sql));
    return ret;
  }

  for (int idx = 0; idx < blobnum; idx++) {
    ret = sqlite3_bind_blob(stmt, idx + 1, blobs[idx]->data, blobs[idx]->nLen,
                            SQLITE_STATIC);
    if (ret != SQLITE_OK) {
      logger_info("Fail to bind blob: %s.", sqlite3_errmsg(sql));
      return -EINVAL;
    }
  }

  ret = sqlite3_step(stmt);
  if (ret != SQLITE_DONE) {
    logger_info("Fail to exec sql: %s", sqlite3_errmsg(sql));
    sqlite3_finalize(stmt);
    return -EINVAL;
  }

  sqlite3_finalize(stmt);

  return 0;
}

static int sqlite_restore(Handle_t handle, const struct Table *table,
                          uint8_t *cond,
                          uint8_t *(*get_buffer)(const struct Table *,
                                                 const void *prim,
                                                 void *oqaue),
                          void *opaque) {
  sqlite3 *sql = (sqlite3 *)handle;
  const struct Elem *prim_elem = NULL;
  char sqlstr[1024] = "\0";
  sqlite3_stmt *stmt = NULL;
  int ret;
  const char *tmpstr;
  uint8_t *base;

  /* 检查是否有primary key，目前仅支持有Prime key的数据库 */
  for (prim_elem = table->elems; prim_elem->key; prim_elem++)
    if (prim_elem->prim) break;

  if (!prim_elem->key) {
    logger_info("No prime key detected. Unsupported db.");
    return -EINVAL;
  }

  sprintf(sqlstr, "SELECT * from %s", table->name);
  if (cond) {
    switch (prim_elem->type) {
      case ET_INT:
        sprintf(sqlstr + strlen(sqlstr), " WHERE %s = %u", prim_elem->key,
                *(int *)(cond + prim_elem->addr));
        break;
      case ET_STRING:
        sprintf(sqlstr, " WHERE %s = '%s'", prim_elem->key,
                get_value_string(cond + prim_elem->addr));
        break;
      default:
        return -EINVAL;
    }
  }
  strcat(sqlstr, ";");

  // logger_info("Query sql: %s", sqlstr);

  ret = sqlite3_prepare_v2(sql, sqlstr, strlen(sqlstr), &stmt, NULL);
  if (ret != SQLITE_OK || !stmt) {
    logger_info("Fail to prepare stmt %s.", sqlstr);
    return -EINVAL;
  }

  do {
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
      logger_info("No data got.");
      sqlite3_finalize(stmt);
      // 如果有给定 condition table 且没查询到数据，将其赋值为默认数据
      if (cond && !get_buffer) fill_table_with_default(table, cond);
      return 0;
    }

    // logger_info("Num of cols: %d", sqlite3_data_count(stmt));

    if (get_buffer) {
      switch (prim_elem->type) {
        case ET_INT:
          ret = sqlite3_column_int(stmt, prim_elem - table->elems);
          base = get_buffer(table, &ret, opaque);
          break;
        case ET_STRING:
          tmpstr = (const char *)sqlite3_column_text(stmt, prim_elem - table->elems);
          base = get_buffer(table, (void *)tmpstr, opaque);
          break;
        default:
          return -EINVAL;
      }
    } else
      base = cond;

    // 没有给定cond和get_buffer,空跑语句
    if (!base) continue;

    for (int idx = 0; idx < sqlite3_data_count(stmt); idx++) {
      struct Blob *blob;
      char **valstr;
      const void *tmpptr;
      const struct Elem *elem = &table->elems[idx];
      switch (elem->type) {
        case ET_INT:
          *(int *)(base + elem->addr) = sqlite3_column_int(stmt, idx);
          break;
        case ET_STRING:
          valstr = (char **)(base + elem->addr);
          tmpstr = (const char *)sqlite3_column_text(stmt, idx);
          if (*valstr) free(*valstr);
          if (tmpstr)
            *valstr = strdup(tmpstr);
          else
            *valstr = NULL;
          break;
        case ET_BLOB:
          tmpptr = sqlite3_column_blob(stmt, idx);
          blob = (struct Blob *)(base + elem->addr);
          blob->nLen = sqlite3_column_bytes(stmt, idx);
          blob->data = malloc(blob->nLen);
          if (!blob->data) {
            blob->nLen = 0;
            logger_error("-----malloc failed-----");
            break;
          }
          memcpy(blob->data, tmpptr, blob->nLen);
          break;
        default:
          break;
      }
    }
    // 如果没有提供get_buffer,查询到一条后即退出查询
    if (!get_buffer) break;
  } while(1);

  sqlite3_finalize(stmt);

  return 0;
}

static int sqlite_delete(Handle_t handle, const struct Table *table,
                         uint8_t *base) {
  sqlite3 *sql = (sqlite3 *)handle;
  const struct Elem *elem = NULL;
  char sqlstr[1024] = "\0";
  char *errmsg = NULL;
  int ret;

  for (elem = table->elems; elem->key; elem++)
    if (elem->prim) break;

  if (!elem->key) {
    logger_info("No prime key detected. Unsupported db.");
    return -EINVAL;
  }

  switch (elem->type) {
    case ET_INT:
      sprintf(sqlstr, "DELETE from %s WHERE %s = %u;", table->name, elem->key,
              *(int *)(base + elem->addr));
      break;
    case ET_STRING:
      sprintf(sqlstr, "DELETE from %s WHERE %s = '%s';", table->name, elem->key,
              get_value_string(base + elem->addr));
      break;
    default:
      return -EINVAL;
  }

  // logger_info("Delete sql: %s", sqlstr);

  ret = sqlite3_exec(sql, sqlstr, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK) {
    logger_info("Fail to exec sql: %s.", errmsg);
    sqlite3_free(errmsg);
    return -EINVAL;
  }

  return 0;
}


int sqlite_transaction(Handle_t handle,
      int (*callback)(Handle_t , const char *table_name),
      Handle_t db_handle, const char *table_name) {
  sqlite3 *sql = (sqlite3 *)handle;
  char *errmsg = NULL;
  int ret;

  if (!callback) return -EINVAL;

  ret = sqlite3_exec(sql, "BEGIN EXCLUSIVE", NULL, NULL, &errmsg);
  if (ret != SQLITE_OK) {
    logger_info("Fail to exec BEGIN EXCLUSIVE: %s.", errmsg);
    sqlite3_free(errmsg);
    return -EINVAL;
  }

  ret = callback(db_handle, table_name);
  if (ret != SQLITE_OK) {
    if (sqlite3_exec(sql, "ROLLBACK", NULL, NULL, &errmsg) != SQLITE_OK) {
      logger_info("Fail to exec ROLLBACK: %s.", errmsg);
      sqlite3_free(errmsg);
    }
    return -EINVAL;
  }

  ret = sqlite3_exec(sql, "COMMIT", NULL, NULL, &errmsg);
  if (ret != SQLITE_OK) {
    logger_info("Fail to exec COMMIT: %s.", errmsg);
    sqlite3_free(errmsg);
    return -EINVAL;
  }

  return 0;
}

static int sqlite_exec_sql(Handle_t handle, const char* sqlstr) {
  sqlite3 *sql = (sqlite3 *)handle;
  char *errmsg = NULL;
  int ret;

  if (!sqlstr) return -EINVAL;

  logger_info("exec sql: %s", sqlstr);

  ret = sqlite3_exec(sql, sqlstr, NULL, NULL, &errmsg);
  if (ret != SQLITE_OK) {
    logger_info("Fail to exec sql: %s.", errmsg);
    sqlite3_free(errmsg);
    return -EINVAL;
  }

  return 0;
}


struct DbDriver sqlite = {
    .name = "sqlite3",
    .open = sqlite_open,
    .close = sqlite_close,
    .create = sqlite_create,
    .update = sqlite_update,
    .restore = sqlite_restore,
    .delete = sqlite_delete,
    .transction = sqlite_transaction,
    .exec_sql = sqlite_exec_sql,
};
