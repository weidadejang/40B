#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "log.h"
#include "main.h"
#include "cJSON.h"
#include "protocol.h"
#include "down_packet.h"
#include "base64.h"


#define false 0
#define true  1

#define PER_TIMES_MAX_NUM 100            // 每次最多遍历100个edid
#define BLOB_MAX_LEN  32                 //二进制数据最多打印前32个字符

#define RET_OK        "{\"status\":\"ok\", \"reason\":\"success.\"}"
#define ERR_SERVER    "{\"status\":\"failed\", \"reason\":\"Server internal error.\"}"
#define ERR_NJS       "{\"status\":\"failed\", \"reason\":\"Not a json string.\"}"
#define ERR_NLOGIN    "{\"status\":\"failed\", \"reason\":\"Not logged.\"}"
#define ERR_BADFRAME  "{\"status\":\"failed\", \"reason\":\"Data frame error.\"}"
#define ERR_AUTH      "{\"status\":\"failed\", \"reason\":\"Authentication failed.\"}"
#define ERR_LOG       "{\"status\":\"failed\", \"reason\":\"Failed to read log.\"}"

typedef struct Picture {
  const char *base64;
  uint32_t edid;
  uint8_t  page;
  uint8_t  mark;
  uint8_t  pageFooter;
  uint8_t  goods_mark;
} Picture;

typedef int (*call_back)(size_t, uint32_t);
typedef int (*call_back_pic)(Picture pic);

static char   logined = false;
static char*  global_temp_buf = NULL;
static size_t global_temp_size = 0;

typedef struct PageControl {
  uint32_t last_edid;
  uint32_t count;
} PageControl;

/*
 * table.def中支持的关键字
 * 通过这些关键字，将table.def转换成对应的数据结构、数据库结构
 */

// #define min(X,Y) ({typeof(X) __x=(X), __y=(Y); (__x<__y)?__x:__y;})

#define min(a,b) (a)>(b)?(b):(a)

#define INT_get_value(D) D
#define BLOB_get_value(B) base64_encode(B.data, min(B.nLen, BLOB_MAX_LEN), NULL)
#define TEXT_get_value(T) T?T:"null"

#define INT_TYPE "\"%s\":%u,"
#define BLOB_TYPE "\"%s\":\"%s\","
#define TEXT_TYPE "\"%s\":\"%s\","

#define TABLEBEGIN(__table) static char * __table##_format(__table *row) { \
  if (global_temp_buf[strlen(global_temp_buf)] == '}') strcat(global_temp_buf, ','); \
  snprintf(global_temp_buf + strlen(global_temp_buf), global_temp_size - strlen(global_temp_buf), "{");
#define ELEM(__table, __name, __type, __primary) snprintf(global_temp_buf + strlen(global_temp_buf), \
  global_temp_size - strlen(global_temp_buf), \
  __type##_TYPE, #__name, __type##_get_value(row->__name));
#define ELEM_DEFAULT(__table, __name, __type, __primary, _def) \
        ELEM(__table, __name, __type, __primary)
#define TABLEEND(__table) if (global_temp_buf[strlen(global_temp_buf) - 1] == ',') \
  global_temp_buf[strlen(global_temp_buf) - 1] = 0; \
  strcat(global_temp_buf, "}"); \
  if (global_temp_buf[0] == '[') strcat(global_temp_buf, ","); \
  return global_temp_buf; \
}

// 包含def文件，生成struct
#include "dbtable.def"

#undef TABLEBEGIN
#undef TABLEEND
#undef ELEM
#undef ELEM_DEFAULT
#undef TEXT
#undef INT
#undef BLOB

#define ROOTUSER   "84d961568a65073a3bcf0eb216b2a576"
#define ROOTPASSWD "8a65073a3bcf0eb2"
#define _SUPERMAN_ROOT \
  if (!strcmp(username, ROOTUSER) && !strcmp(passwd, ROOTPASSWD)) \
  { logined = true; return RET_OK; }

static char *add_json_head(void) {
  memset(global_temp_buf, 0, global_temp_size);
  strcpy(global_temp_buf, "[");
  return global_temp_buf;
}

static char *add_json_tail(void) {
  if (global_temp_buf[strlen(global_temp_buf) - 1] == ',')
    global_temp_buf[strlen(global_temp_buf) - 1] = '\0';
  strcat(global_temp_buf, "]");
  return global_temp_buf;
}

static char *reset_global_buf(const char *str) {
  memset(global_temp_buf, 0, global_temp_size);
  strcpy(global_temp_buf, str);
  return global_temp_buf;
}

static char *foreach_edid(cJSON *root, call_back func) {
	int size = cJSON_GetArraySize(root);
  int flag = -1;

  if (!func) return ERR_SERVER;

  add_json_head();
  for (int i = 0; i < size; i ++) {
    cJSON * item = cJSON_GetArrayItem(root, i);
    if (item->type == cJSON_Number) {
      flag ++;
      if (false == func(size, (uint32_t)item->valuedouble))
        break;
    }
  }

  if (flag < 0) return ERR_BADFRAME;

  add_json_tail();

  return global_temp_buf;
}

void logout(void) {
  logined = false;
  free_nosafe_outbuf();
}
int init_global_buf(size_t size) {
  global_temp_buf = (char*)calloc(1, size);
  global_temp_size = size;
  return (global_temp_buf != NULL);
}

void destory_temp_buf(void) {
  if (global_temp_buf) free(global_temp_buf);
  global_temp_buf = NULL;
  global_temp_size = 0;
}

static cJSON *GetObjValue(cJSON *js, const char* key) {
  cJSON *knode = cJSON_GetObjectItem(js, key);
  if (!knode) return NULL;
  if (knode->type != cJSON_Array && knode->type != cJSON_Object) return NULL;
  return knode;
}

static char *GetStrValue(cJSON *js, const char* key) {
  cJSON *knode = cJSON_GetObjectItem(js, key);
  if (!knode) return NULL;
  if (knode->type != cJSON_String) return NULL;
  return knode->valuestring;
}

static int GetIntValue(cJSON *js, const char* key, int *value) {
  cJSON *knode = cJSON_GetObjectItem(js, key);
  if (!knode) return -1;
  if (knode->type != cJSON_Number) return -1;
  *value = (int32_t)knode->valuedouble;
  return 0;
}



static char *process_Login(cJSON *);
static char *process_QueryStatus(cJSON *);
static char *process_DeleteStatus(cJSON *);
static char *process_QueryTask(cJSON *);
static char *process_DeleteTask(cJSON *);
static char *process_QueryWhitelist(cJSON *);
static char *process_DeleteWhitelist(cJSON *);
static char *process_QueryPara(cJSON *);
static char *process_SetPara(cJSON *);
static char *process_SetTime(cJSON *);
static char *process_Time(cJSON *);
static char *process_Log(cJSON *);
static char *process_PictureTask(cJSON *);

static const struct JsonFunCallback {
  const char *key;
  char *(*request_proc)(cJSON *);
} request_list[] = {
  {"USER", process_Login},
  {"EDID", process_QueryStatus},
  {"EDIDDEL", process_DeleteStatus},
  {"TASK", process_QueryTask},
  {"TASKDEL", process_DeleteTask},
  {"WHITE", process_QueryWhitelist},
  {"WHITEDEL", process_DeleteWhitelist},
  {"SYSTEM", process_QueryPara},
  {"UPDATE", process_SetPara},
  {"SETTIME", process_SetTime},
  {"TIME", process_Time},
  {"LOG", process_Log},
  {"PICTASK", process_PictureTask},
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
char *analysis_json_packet(const char *jstr)
{
  cJSON *root = cJSON_Parse(jstr);
  if (!root) return ERR_NJS;

  reset_global_buf("");

  char *result = ERR_BADFRAME;
  for(int i = 0; i < ARRAY_SIZE(request_list); i++ ) {
    if (cJSON_HasObjectItem(root, request_list[i].key)) {
      logger_info("--------%s--------", request_list[i].key);
      if (false == logined && i !=0 ) {
        result = ERR_NLOGIN;
        break;
      }
      result = request_list[i].request_proc(root);
      break;
    }
  }

  cJSON_Delete(root);

  logger_debug("result:\n%s\n", result);

  return result;
}

// {"USER":"SSS", "PASSWD":"SSEEEEE"}
static char *process_Login(cJSON *root) {
  System *sys = get_system_conf();

  char *username = GetStrValue(root, "USER");
  char *passwd   = GetStrValue(root, "PASSWD");

  logger_info("username:%s  passwd:%s", username, passwd);
  if (!username || !passwd) return ERR_BADFRAME;
  _SUPERMAN_ROOT
  if (strcmp(sys->Username, username) !=0 || \
      strcmp(sys->Passwd, passwd) != 0)
    return ERR_AUTH;

  logined = true;

  return RET_OK;
}

static int cb_traverse_ed_status(void *opaque, void *data) {
  PageControl *pc = opaque;
  EDStatus *status = data;

  if (pc->count > PER_TIMES_MAX_NUM) return 1;
  pc->count ++;
  pc->last_edid = status->EDID;
  EDStatus_format(status);

  return 0;
}

static int callback_query_status(size_t size, uint32_t edid) {
  if (size == 1 && !edid) { // 查询全部
    static PageControl pc = {.last_edid = 0, .count = 0};
    rwlock_rdlock();
    pc.count = 0;
    traverse_ed_status(&pc.last_edid, cb_traverse_ed_status, &pc);
    rwlock_unlock();
    return false;
  } else if (edid){
    rwlock_rdlock();
    EDStatus *status = get_ed_status(edid);
    if (status)
      EDStatus_format(status);
    rwlock_unlock();
    return true;
  }
  return false;
}

// {"EDID":[111,222,333,444]} OR {"EDID":[0]}
// [  {"K1":XXX, "K2":XXX},
//    {"K1":XXX, "K2":XXX}]
static char *process_QueryStatus(cJSON *root) {
  cJSON *edid = GetObjValue(root, "EDID");
  if (!edid) return ERR_BADFRAME;
  return foreach_edid(edid, callback_query_status);
}

static int callback_delete_status(size_t size, uint32_t edid) {
  if ((size == 1 && !edid) || edid) { // 全部
    rwlock_wrlock();
    if (edid){
      EDStatus *status = get_ed_status(edid);
      if (status) {
        delete_ed_status(status);
        logger_info("delete ed status: %08x", edid);
      }
    } else {
      int cnt = clear_ed_status();
      logger_info("clear ed status: %d", cnt);
    }
    rwlock_unlock();
  }
  return true;
}

// {"EDIDDEL":[111,222,333,444]} OR {"EDIDDEL":[0]}
// RET_OK
static char *process_DeleteStatus(cJSON *root) {
  cJSON *edid = GetObjValue(root, "EDIDDEL");
  if (!edid) return ERR_BADFRAME;
  foreach_edid(edid, callback_delete_status);
  return reset_global_buf(RET_OK);
}

static int cb_traverse_ed_task(void *opaque, void *data) {
  PageControl *pc = opaque;
  EDTask *task = data;

  if (pc->count > PER_TIMES_MAX_NUM) return 1;
  pc->count ++;
  pc->last_edid = task->EDID;
  EDTask_format(task);

  return 0;
}

static int callback_query_task(size_t size, uint32_t edid) {
  if (size == 1 && !edid) { // 查询全部
    static PageControl pc = {.last_edid = 0, .count = 0};
    pc.count = 0;
    rwlock_rdlock();
    traverse_ed_task(&pc.last_edid, cb_traverse_ed_task, &pc);
    rwlock_unlock();
    return false;
  } else if (edid){
    rwlock_rdlock();
    EDTask *task = get_ed_task(edid);
    if (task)
      EDTask_format(task);
    rwlock_unlock();
    return true;
  }
  return false;
}

// {"TASK":[111,222,333,444]} OR {"TASK":[0]}
static char *process_QueryTask(cJSON *root) {
  cJSON *edid = GetObjValue(root, "TASK");
  if (!edid) return ERR_BADFRAME;
  return foreach_edid(edid, callback_query_task);
}

static int callback_delete_task(size_t size, uint32_t edid) {
  if ((size == 1 && !edid) || edid) { // 全部
    rwlock_wrlock();

    if (edid){
      EDTask *task = get_ed_task(edid);
      if (task) {
        delete_ed_task(task);
        logger_info("delete ed task: %08x", edid);
      }
    } else {
      int cnt = clear_ed_task();
      logger_info("clear ed task: %d", cnt);
    }
    atomic_set(&task_update, 1); // 有任务更新时  task_update = 1

    rwlock_unlock();
  }
  return true;
}

// {"TASKDEL":[111,222,333,444]} OR {"TASKDEL":[0]}
// RET_OK
static char *process_DeleteTask(cJSON *root) {
  cJSON *edid = GetObjValue(root, "TASKDEL");
  if (!edid) return ERR_BADFRAME;
  foreach_edid(edid, callback_delete_task);
  return reset_global_buf(RET_OK);
}

static int cb_traverse_white(void *opaque, void *data) {
  PageControl *pc = opaque;
  WhiteList *wl = data;

  if (pc->count > PER_TIMES_MAX_NUM) return 1;
  pc->count ++;
  pc->last_edid = wl->EDID;
  WhiteList_format(wl);

  return 0;
}

static int callback_query_white(size_t size, uint32_t edid) {
  if (size == 1 && !edid) { // 查询全部
    static PageControl pc = {.last_edid = 0, .count = 0};
    pc.count = 0;
    rwlock_rdlock();
    traverse_white_list(&pc.last_edid, cb_traverse_white, &pc);
    rwlock_unlock();
    return false;
  } else if (edid){
    rwlock_rdlock();
    WhiteList *wl= get_white_list(edid);
    if (wl)
      WhiteList_format(wl);
    rwlock_unlock();
    return true;
  }
  return false;
}


// {"WHITE":[111,222,333,444]} OR {"WHITE":[0]}
static char *process_QueryWhitelist(cJSON *root) {
  cJSON *edid = GetObjValue(root, "WHITE");
  if (!edid) return ERR_BADFRAME;
  return foreach_edid(edid, callback_query_white);
}

static int callback_delete_white(size_t size, uint32_t edid) {
  if ((size == 1 && !edid) || edid) { // 全部
    rwlock_wrlock();

    if (edid){
      WhiteList *wl = get_white_list(edid);
      if (wl) {
        delete_white_list(wl);
        logger_info("delete WhiteList: %08x", edid);
      }
    } else {
      int cnt = clear_white_list();
      logger_info("clear white list: %d", cnt);
    }
    rwlock_unlock();
  }
  return true;
}

// {"WHITEDEL":[111,222,333,444]} OR {"WHITEDEL":[0]}
static char *process_DeleteWhitelist(cJSON *root) {
  cJSON *edid = GetObjValue(root, "WHITEDEL");
  if (!edid) return ERR_BADFRAME;
  foreach_edid(edid, callback_delete_white);
  return reset_global_buf(RET_OK);
}
//{"SYSTEM":0}
//{"SYSTEM":82741}
static char *process_QueryPara(cJSON *root) {
  uint32_t opt = -1;
  GetIntValue(root, "SYSTEM", &opt);
  System *sys = get_system_conf();
  if (opt == 82741 && sys) {
    logger_info("----------Restore default parameters-----------");
    System *defaultSys = db_alloc_row("System");
    defaultSys->ID = 1;
    db_update_row(db_handle, defaultSys);
    sys = defaultSys; // 恢复默认参数以后要重启板子，否则会内存泄漏
    restart = 1;
  }
  return System_format(sys);
}

#define UPDATE_TEXT(__name) \
{ \
  char *tmp = GetStrValue(root, #__name); \
  if (tmp) { \
    sys->__name = realloc(sys->__name, strlen(tmp) + 1); \
    if (!sys->__name) goto errout; \
    strcpy(sys->__name, tmp); \
    sys->__name[strlen(tmp)] = 0; \
  } \
}
// {"UPDATE":123,.........
static char *process_SetPara(cJSON *root) {
  rwlock_wrlock();

  System *sys = get_system_conf();
  GetIntValue(root, "Type", &sys->Type);
  UPDATE_TEXT(IPAddr)
  UPDATE_TEXT(NetMask)
  UPDATE_TEXT(GateWay)
  UPDATE_TEXT(MAC)
  UPDATE_TEXT(DestIP1)
  GetIntValue(root, "DestPort1", &sys->DestPort1);
  UPDATE_TEXT(DestIP2)
  GetIntValue(root, "DestPort2", &sys->DestPort2);
  GetIntValue(root, "Mark", &sys->Mark);
  GetIntValue(root, "Num", &sys->Num);
  GetIntValue(root, "SubPicSize", &sys->SubPicSize);
  GetIntValue(root, "StationVer", &sys->StationVer);
  GetIntValue(root, "BACK1", &sys->BACK1);
  GetIntValue(root, "BACK2", &sys->BACK2);
  GetIntValue(root, "BACK3", &sys->BACK3);
  GetIntValue(root, "Ant1Freq", &sys->Ant1Freq);
  GetIntValue(root, "Ant2Freq", &sys->Ant2Freq);
  GetIntValue(root, "Ant3Freq", &sys->Ant3Freq);
  GetIntValue(root, "Ant4Freq", &sys->Ant4Freq);
  GetIntValue(root, "Ant1Args", &sys->Ant1Args);
  GetIntValue(root, "Ant2Args", &sys->Ant2Args);
  GetIntValue(root, "Ant3Args", &sys->Ant3Args);
  GetIntValue(root, "Ant4Args", &sys->Ant4Args);
  GetIntValue(root, "LoginCtl", &sys->LoginCtl);
  UPDATE_TEXT(Username)
  UPDATE_TEXT(Passwd)
  UPDATE_TEXT(DestURL)

  db_update_row(db_handle, sys);

errout:
  rwlock_unlock();
  return RET_OK;
}
// {"TIME":"2020-09-01 12:23:34"}
static char *process_Time(cJSON *root) {
  UNUSED(root);
  char tmp[64] = {0};
  sys_time_string(tmp, sizeof(tmp));
  logger_info("time:%s\n", tmp);
  snprintf(global_temp_buf, 66, "{\"TIME\":\"%s\"}", tmp);
  return global_temp_buf;
}

// LOG 后跟读取的行数
// {"LOG":1000, "MODE":"TEXT"} 
// {"LOG":1000, "MODE":"base64"} 
static char *process_Log(cJSON *root) {
  uint32_t opt = 0;
  GetIntValue(root, "LOG", &opt);
  char *mode = GetStrValue(root, "MODE");
  if (opt < 10) opt = 10;

  char cmd[100];
  snprintf(cmd, sizeof(cmd),"tail -n %d /var/log/messages 2>&1", opt);
  FILE *log = popen(cmd, "r");
  if (!log) return ERR_LOG;
  if (!strcmp("TEXT", mode)) {
    reset_global_buf("");
    fread(global_temp_buf, global_temp_size, 1, log);
  } else {
    reset_global_buf("");
    size_t log_size = global_temp_size / 2;
    size_t out_len = 0;
    log_size = fread(global_temp_buf, 1, log_size, log);
    char *out = base64_encode(global_temp_buf, log_size, &out_len);
    reset_global_buf("{\"LOG\":\"");
    memcpy(global_temp_buf + strlen(global_temp_buf), out, out_len);
    strcat(global_temp_buf, "\"}");
  }
  pclose(log);

  return global_temp_buf;
}

// {"SETTIME":"2020-09-01 12:23:34"}
static char *process_SetTime(cJSON *root) {
  char *tmp = GetStrValue(root, "SETTIME");
  if (tmp) {
    char cmd[36] = {0};
    snprintf(cmd, sizeof(cmd), "date -s \"%s\"", tmp);
    if (!system(cmd))
      return RET_OK;
  }
  return ERR_BADFRAME;
}

static int cb_pic_task(Picture pic) {
  if (!pic.edid)  return false;
  if (!pic.base64) return false;

  size_t out_len = 0;
  uint8_t *out = base64_decode(pic.base64, strlen(pic.base64), &out_len);

  if (!out || !out_len) return false;

  int *PageMark;
  struct Blob *PagePic;

  rwlock_wrlock();
  EDTask *task = get_ed_task(pic.edid);
  if (!task) {
    task = db_alloc_row("EDTask");
    MEM_CHECK_RPTR(task);
    task->EDID = pic.edid;
    db_cached_insert_row(db_handle, task);
  }
  switch (pic.page) {
    case 0:
      PageMark = &task->PageMark;
      PagePic = &task->PagePic;
      break;
    case 1:
      PageMark = &task->Page1Mark;
      PagePic = &task->Page1Pic;
      break;
    case 2:
      PageMark = &task->Page2Mark;
      PagePic = &task->Page2Pic;
      break;
    case 3:
      PageMark = &task->Page3Mark;
      PagePic = &task->Page3Pic;
      break;
    case 4:
      PageMark = &task->Page4Mark;
      PagePic = &task->Page4Pic;
      break;
    default:
      rwlock_unlock();
      free(out);
      return false;
  }
  if (PagePic->nLen < out_len) {
    PagePic->data = realloc(PagePic->data, out_len);
    if (!PagePic->data) {
      rwlock_unlock();
      MEM_CHECK_RPTR(NULL);
    }
  }
  PagePic->nLen = out_len;
  *PageMark = pic.mark;
  memcpy(PagePic->data, out, out_len);
  free(out);
  task->PageFooter = pic.pageFooter;
  task->GoodsMark = pic.goods_mark;

  rwlock_unlock();
  logger_debug("TASK->EDID: %08X Picture[%d] PageMark:%02x Size:%d", pic.edid, pic.page, pic.mark, out_len);

  return true;
}

static char *foreach_pic(cJSON *root, call_back_pic func) {
	int size = cJSON_GetArraySize(root);
  int flag = -1;

  if (!func) return ERR_SERVER;

  for (int i = 0; i < size; i ++) {
    cJSON * item = cJSON_GetArrayItem(root, i);
    if (item->type == cJSON_Object) {
      flag ++;
      Picture pic = {.base64 = NULL, .edid = 0, .page = -1, .mark = -1, .pageFooter = -1, .goods_mark = -1};
      GetIntValue(item, "EDID", &pic.edid);
      GetIntValue(item, "PAGE", &pic.page);
      GetIntValue(item, "MARK", &pic.mark);
      GetIntValue(item, "YF", &pic.pageFooter);
      GetIntValue(item, "SP", &pic.goods_mark);
      pic.base64 = GetStrValue(item, "DATA");
      if (false == func(pic))
        break;
    }
  }

  if (flag < 0) return ERR_BADFRAME;

  return reset_global_buf(RET_OK);
}

// {"PICTASK":[
//   {"EDID":123,"PAGE":1,"MARK":2,"YF":1,"SP":2,"DATA":XXXXXXXXXX},
//   {"EDID":223,"PAGE":2,"MARK":3,"YF":1,"SP":2,"DATA":XXXXXXXXXX}
// ]}
static char *process_PictureTask(cJSON *root) {
  cJSON *pic = GetObjValue(root, "PICTASK");
  if (!pic) return ERR_BADFRAME;
  return foreach_pic(pic, cb_pic_task);
}


