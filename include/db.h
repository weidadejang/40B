
#ifndef DB_H
#define DB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

  
/**
 * 封装了DB基本的操作接口，通过table.def作为入口，支持db文件读写和mem读写。
 * @param name
 * @return 
 */
Handle_t db_open(const char *name);
void db_close(Handle_t *handle);

/**
 * 检查表是否存在，不存在则创建。根据db_name去table.def中寻找对应的规则。
 * 如果在表规则中声明了cache模式，则在内存中建立对应的映射。
 * @param handle    the handle return by @db_open
 * @param db_name
 * @return 0 on success
 */
int db_create_table(Handle_t handle, const char *table_name);
/* 将缓存数据与硬盘进行同步 */
int db_cached_sync(Handle_t handle, const char *table_name);
/**
 * db_{execution}_row系列函数对数据库进行直接操作，影响到硬盘中的数据。
 * @param handle    the handle return by @db_open
 * @param pRow
 * @return 0 on success
 */
/* 从DB文件重新读取数据 */
int db_restore_row(Handle_t handle, void *pRow);
/* 更新/插入DB文件中此行数据 */
int db_update_row(Handle_t handle, void *pRow);
/* 从DB文件中删除此行 */
int db_delete_row(Handle_t handle, void *pRow);
/**
 * db_cached_{exec}_row系列函数对缓存的数据库进行操作，不影响硬盘中数据。
 * @param handle    the handle return by @db_open
 * @param prim_val  主key的值，根据此值查询cache中是否有此行
 * @return NULL if not found in cache
 */
void *db_cached_get_row(Handle_t handle, const char *table_name, void *prim_val);
/* 在cache里插入一行数据 */
int db_cached_insert_row(Handle_t handle, void *pRow);
/* 从cache里删除一行数据 */
void db_cached_delete_row(Handle_t handle, void *pRow);
/* clear table of cached list */
int db_cached_clear_table(Handle_t handle, const char *table_name);

/**
 * 遍历一整张表
 * @param handle
 * @param table_name
 * @param prim_val    从某个点开始，NULL则从头开始。
 * @param cb          callback函数，传入数据为table对应的数据结构。返回非0值则终止遍历。
 * @param opaque
 * @return the number of rows been traversed
 */
typedef int (*trav_cb)(void *opaque, void *pRow);
int db_cached_traverse_row(Handle_t handle, const char *table_name,
                           void *prim_val, trav_cb cb, void *opaque);

/**
 * 分配或释放表的一列内存空间。此操作不会对cache的表造成影响。
 * alloc和free必须要成对使用！！！
 * @param db_name
 * @return NULL on failure
 */
void *db_alloc_row(const char *table_name);
void db_free_row(void *pRow);
/**
 * 打印表的一行数据。
 * @param pRow
 */
void db_dump_row(const void *pRow);

// 用于执行特殊的sql语句，提高db模块的灵活性
int db_exec_sqlstr(Handle_t handle, const char* sqlstr);

// 对应数据库数据类型的BLOB
struct Blob {
  size_t nLen;
  void *data;
};

/*
 * table.def中支持的关键字
 * 通过这些关键字，将table.def转换成对应的数据结构、数据库结构
 */
#define INT int
#define BLOB struct Blob
#define TEXT char *

#define TABLEBEGIN(__table) typedef struct __table {
#define ELEM(__table, __name, __type, __primary) __type __name;
#define ELEM_DEFAULT(__table, __name, __type, __primary, _def) \
        ELEM(__table, __name, __type, __primary)
#define TABLEEND(__table) } __PACKED __table;

// 包含def文件，生成struct
#include "dbtable.def"

#undef TABLEBEGIN
#undef TABLEEND
#undef ELEM
#undef ELEM_DEFAULT
#undef TEXT
#undef INT
#undef BLOB

#ifdef __cplusplus
}
#endif

#endif /* DB_H */
