#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "crc.h"
#include "mac.h"
#include "sysinfo.h"
#include "main.h"
#include "protocol.h"
#include "common.h"
#include "up_packet.h"


//#define MAX_EXPIRED_TIME  (24*60*60) //24 Hour
#define MAX_EXPIRED_TIME  (192*60*60) //24 Hour
#define MAX_SCAN_NUM      256
struct ExpiredStatus{
  uint32_t edid[MAX_SCAN_NUM];
  uint16_t idx;
} expired_status = {
  .idx = 0,
};

struct frame_prop {
  uint8_t *frame;
  size_t index;
  int task_num;
  uint32_t last_edid;
};

static WhiteList *query_or_create_white_list(int edid) {
  WhiteList *wl = get_white_list(edid);
  if (!wl) {
    wl = db_alloc_row("WhiteList");
    MEM_CHECK_RPTR(wl);
    wl->EDID = edid;
    db_cached_insert_row(db_handle, wl);
  }

  return wl;
}


static EDTask *query_or_create_task(int edid) {
  EDTask *task = get_ed_task(edid);
  if (!task) {
    task = db_alloc_row("EDTask");
    MEM_CHECK_RPTR(task);
    task->EDID = edid;
    db_cached_insert_row(db_handle, task);
  }

  atomic_set(&task_update, 1); // 有任务更新时  task_update = 1
  return task;
}

// 局部图解析
UpPacket *analysis_JBT(u8_t *data, unsigned int size, TcpContext *TcpCtx) {

  time_t t = time(NULL);
  struct tm res;
  TcpHeader2 *header = (TcpHeader2*)data;

  MEM_CHECK_RPTR(TcpCtx);
  MEM_CHECK_RPTR(TcpCtx->up_packet);

  if (size < header->len + sizeof(*header)) return NULL;

  Cmd0x57 *cmd = (Cmd0x57*)(data + sizeof(TcpHeader2));
  PictureData *PicHead = (PictureData*)cmd->JQ;


  ResponseHead *resp_head = (ResponseHead*)TcpCtx->up_packet->data;
  resp_head->head = 0xfafa;
  resp_head->fun_code = header->fun_code;
  Response0x5X *resp = (Response0x5X*)resp_head->data;
  resp->num = 0;

  localtime_r(&t, &res);



  int task_cnt = 0;
  for (int i = 0; i < cmd->num; i++) {
    EDTask *task;
    if (PicHead->Head == 0xe0e0) {
      uint32_t PicPacket_len = 0;
      memcpy(&PicPacket_len, &PicHead->Len, sizeof(PicHead->Len));
      END_TYPE *end_code =  (END_TYPE*)((uint8_t*)&PicHead->Page + PicPacket_len);
      int *PageMark;
      struct Blob *PagePic;
      if (*end_code != 0xe1e1) {
        logger_warning("------data error--------");
        return NULL;
      }
      rwlock_wrlock();
      task = query_or_create_task(PicHead->EDID);
      if (!task) {
        rwlock_unlock();
        logger_error("create task node error.");
        return NULL;
      }

      for (int m =0; m < 5; m ++) {
        if (!(PicHead->Page & (1<<m))) continue;
        switch (m) {
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
        }
        if (PagePic->nLen < PicPacket_len) {
          PagePic->data = realloc(PagePic->data, PicPacket_len);
          if (!PagePic->data) {
            rwlock_unlock();
            MEM_CHECK_RPTR(NULL);
          }
        }
        PagePic->nLen = PicPacket_len;
        memcpy(PagePic->data, &PicHead->Page, PicPacket_len);
        task->PageFooter = PicHead->PageFooter;
        task->GoodsMark = PicHead->goods_mark;

        task->SuccessFlag = 0;
        task->Hour 	= res.tm_hour;
        task->Min 	= res.tm_min;
        task->Sec 	= res.tm_sec;
        task->Day   = res.tm_mday;  //day in month (11/30)

        *PageMark = PicHead->PageMark;
        task_cnt ++;
        logger_debug("TASK->EDID: %08X %u Picture[%d] PageMark:%02x Size:%d", PicHead->EDID,PicHead->EDID, m, PicHead->PageMark, PagePic->nLen);
      }
      rwlock_unlock();
      resp->num++;
      resp->EDID[i] = PicHead->EDID;
      PicHead = (PictureData*)((uint8_t*)&PicHead->Page + PicPacket_len + 2);
    } else {
      logger_warning("------data error--------");
      return NULL;
    }
  }
  logger_debug("<<<--------------------Number of tasks:%d  ---  CurrentTask Timestamp:0x%02X-0x%02X-0x%02X-0x%02X , %d-%d-%d-%d------------------>>>", \
		  task_cnt,res.tm_mday,res.tm_hour,res.tm_min,res.tm_sec,res.tm_mday,res.tm_hour,res.tm_min,res.tm_sec);
  resp_head->len = sizeof(Response0x5X) + resp->num * 4;

  CRC_TYPE *crc = (CRC_TYPE*)(resp_head->data + resp_head->len);
  *crc = MBCRC16(&resp_head->fun_code, resp_head->len + sizeof(ResponseHead) - 2);
  END_TYPE *end_code = (END_TYPE *)(resp_head->data + resp_head->len + 2);
  *end_code = 0xfbfb;
  TcpCtx->up_packet->len = sizeof(ResponseHead) + resp_head->len + 2;

  return TcpCtx->up_packet;
}

int update_d_table(Cmd0x51 *cmd, uint16_t len) {
  System *sysconf = get_system_conf();
  uint8_t *data = cmd->data;
  int i = 0;

  sysconf->Type = data[i++];
  sysconf->IPAddr = realloc(sysconf->IPAddr, 16);
  MEM_CHECK_R_1(sysconf->IPAddr);
  snprintf(sysconf->IPAddr, 16, "%d.%d.%d.%d", data[i+0], data[i+1], data[i+2],
           data[i+3]);
  i += 4;
  if (0 != is_valid_ip(sysconf->IPAddr)) {
    logger_warning("%s : Parameters are illegal.", sysconf->IPAddr);
  }
  sysconf->NetMask = realloc(sysconf->NetMask, 16);
  MEM_CHECK_R_1(sysconf->NetMask);
  snprintf(sysconf->NetMask, 16, "%d.%d.%d.%d", data[i+0], data[i+1], data[i+2],
           data[i+3]);
  i += 4;
  sysconf->GateWay = realloc(sysconf->GateWay, 16);
  MEM_CHECK_R_1(sysconf->GateWay);
  snprintf(sysconf->GateWay, 16, "%d.%d.%d.%d", data[i+0], data[i+1], data[i+2],
           data[i+3]);
  i += 4;
  if (0 != is_valid_ip(sysconf->GateWay)) {
    logger_warning("%s : Parameters are illegal.", sysconf->GateWay);
  }
  sysconf->MAC = realloc(sysconf->MAC, 18);
  MEM_CHECK_R_1(sysconf->MAC);
  snprintf(sysconf->MAC, 18, "%02x:%02x:%02x:%02x:%02x:%02x", data[i+0], data[i+1],
           data[i+2], data[i+3], data[i+4], data[i+5]);
  i += 6;
  if (0 != is_valid_mac(sysconf->MAC)) {
    logger_warning("%s : Parameters are illegal.", sysconf->MAC);
  }

  // server ip; maybe is url string.
  size_t url_len = strlen((char*)(data + i)) + 1;
  if (url_len > (size_t)(len - i)) return -1;

  sysconf->DestIP1 = realloc(sysconf->DestIP1, url_len);
  MEM_CHECK_R_1(sysconf->DestIP1);
  snprintf(sysconf->DestIP1, url_len, "%s", data + i);
  i += url_len;
  sysconf->DestPort1 = data[i] | data[i+1] << 8;
  i += 2;

  url_len = strlen((char*)(data + i)) + 1;
  if (url_len > (size_t)(len - i)) return -1;
  sysconf->DestIP2 = realloc(sysconf->DestIP2, url_len);
  MEM_CHECK_R_1(sysconf->DestIP2);
  snprintf(sysconf->DestIP2, url_len, "%s", data + i);
  i += url_len;
  sysconf->DestPort2 = data[i] | data[i+1] << 8;
  i += 2;

  sysconf->Mark = data[i++];
  sysconf->Num = data[i++];
  sysconf->SubPicSize = data[i] | data[i + 1] << 8;
  i += 2;

  sysconf->StationVer = data[i++];
  sysconf->BACK1 = data[i++];
  sysconf->BACK2 = data[i++];
  sysconf->BACK3 = data[i++];
  sysconf->Ant1Freq = data[i] | data[i + 1] << 8;
  i += 2;
  sysconf->Ant2Freq = data[i] | data[i + 1] << 8;
  i += 2;
  sysconf->Ant3Freq = data[i] | data[i + 1] << 8;
  i += 2;
  sysconf->Ant4Freq = data[i] | data[i + 1] << 8;
  i += 2;
  sysconf->Ant1Args = data[i] | data[i + 1] << 8 | data[i + 2] << 16 | data[i + 3] << 24;
  i += 4;
  sysconf->Ant2Args = data[i] | data[i + 1] << 8 | data[i + 2] << 16 | data[i + 3] << 24;
  i += 4;
  sysconf->Ant2Args = data[i] | data[i + 1] << 8 | data[i + 2] << 16 | data[i + 3] << 24;
  i += 4;
  sysconf->Ant3Args = data[i] | data[i + 1] << 8 | data[i + 2] << 16 | data[i + 3] << 24;
  i += 4;

  sysconf->LoginCtl = data[i++];
  // username
  url_len = strlen((char*)(data + i)) + 1;
  if (url_len > (size_t)(len - i)) return -1;
  sysconf->Username = realloc(sysconf->Username, url_len);
  MEM_CHECK_R_1(sysconf->Username);
  snprintf(sysconf->Username, url_len, "%s", data + i);
  i += url_len;
  //password
  url_len = strlen((char*)(data + i)) + 1;
  if (url_len > (size_t)(len - i)) return -1;
  sysconf->Passwd = realloc(sysconf->Passwd, url_len);
  MEM_CHECK_R_1(sysconf->Passwd);
  snprintf(sysconf->Passwd, url_len, "%s", data + i);
  i += url_len;

  int this_url_len = len - i + 1;
  sysconf->DestURL = realloc(sysconf->DestURL, this_url_len);
  MEM_CHECK_R_1(sysconf->DestURL);
  if (sysconf->DestURL) snprintf(sysconf->DestURL, this_url_len, "%s", data + i);
  i += this_url_len;


  if (db_update_row(db_handle, sysconf) < 0) return -1; 

  return i;
}

int update_nfc_table(NFCData *nfc) {
  EDTask *task = query_or_create_task(nfc->EDID);

  ASSERT(task != NULL);

  task->NFCMark = nfc->NFCMark;

  task->NFCData = realloc(task->NFCData, nfc->Len + 1);
  MEM_CHECK_R_1(task->NFCData);
  strncpy((char*)task->NFCData, (const char *)nfc->Data, nfc->Len);
  task->NFCData[nfc->Len] = '\0';

  return 0;
}

int delete_task(u32_t edid) {
  EDTask *task = NULL;
  if (edid){
    task = get_ed_task(edid);
    if (task) {
      delete_ed_task(task);
      logger_info("delete ed task: %08x", edid);
    }
  } else {
    int cnt = clear_ed_task();
    logger_info("clear ed task: %d", cnt);
  }
  atomic_set(&task_update, 1); // 有任务更新时  task_update = 1

  return 0;
}

int delete_status(uint32_t edid) {
  EDStatus *status = NULL;
  if (edid){
    status = get_ed_status(edid);
    if (status) {
      delete_ed_status(status);
      logger_info("delete ed status: %08x", edid);
    }
  } else {
    int cnt = clear_ed_status();
    logger_info("clear ed status: %d", cnt);
  }
  return 0;
}

int update_qieye_table(u32_t edid, u8_t mask) {
  EDTask *task = query_or_create_task(edid);

  ASSERT(task != NULL);

  task->PageChange = mask;

  return 0;
}

int update_white_list(u32_t edid, u8_t flag) {
  WhiteList *wl = query_or_create_white_list(edid);

  ASSERT(wl != NULL);

  wl->Flag = flag;

  System *sys = get_system_conf();
  if (flag) {
    sys->WhiteNum ++;
  } else {
    sys->WhiteNum ++;
    if (sys->WhiteNum < 0) sys->WhiteNum = 0;
  }

  return 0;
}

int update_led_table(LEDData *led) {
  EDTask *task = query_or_create_task(led->EDID);

  ASSERT(task != NULL);

  task->LedMark = led->LedMark;
  task->Led1Mark = led->Led1Mark;
  task->Led2Mark = led->Led2Mark;
  task->Led3Mark = led->Led3Mark;
  task->Led4Mark = led->Led4Mark;

  return 0;
}

int query_task(void *opaque, void *data) {
  struct frame_prop *prop = opaque;
  EDTask *task = data;
  int index = prop->index;
  uint8_t *frame = prop->frame;
  EDStatus *status = get_ed_status(task->EDID);
  EDStatus tmp_status;

  prop->last_edid = task->EDID;
  if (prop->task_num == MAX_NUM_OF_TASK) return 1; 

  if (!status) {
    status = &tmp_status;
    memset(status, 0 , sizeof(EDStatus));
  }

  if(task->Day || task->Hour || task->Min || task->Sec)  //
  {
    prop->task_num ++;
    frame[index++] = task->EDID;
    frame[index++] = task->EDID >> 8;
    frame[index++] = task->EDID >> 16;
    frame[index++] = task->EDID >> 24;

    logger_debug("QueryTask[%08x %08u] EDMark:TaskMark [%d:%d--%d:%d--%d:%d--%d:%d]", task->EDID, task->EDID, \
      status->TaskTimeMark1, task->Day, status->TaskTimeMark2, task->Hour, \
      status->TaskTimeMark3, task->Min, status->TaskTimeMark4, task->Sec);

    if((status->TaskTimeMark1 == task->Day) && (status->TaskTimeMark2 == task->Hour) \
    && (status->TaskTimeMark3 == task->Min)&& (status->TaskTimeMark4 == task->Sec))
    {
    	frame[index++] = 1;
    	task->SuccessFlag = 1;
    }
    else {
    	frame[index++] = 0;
    	task->SuccessFlag = 0;
    }
    frame[index++] = 0x57;
  }

  prop->index = index;

  return 0;
}

int makeup_a_table(void *opaque, void *data) {
  EDStatus *status = data;
  struct frame_prop *prop = opaque;
  uint8_t *buf = prop->frame;
  int i = prop->index;
  int v1, v2, v3, v4, v5;

  if (prop->task_num == MAX_NUM_OF_JQ) return 1;
  prop->last_edid = status->EDID;
  prop->task_num ++;
  buf[i++] = 0xee;
  buf[i++] = 0xee;
  int pc = i;
  buf[i++] = 0;
  buf[i++] = 0;

  buf[i++] = status->EDID;
  buf[i++] = status->EDID >> 8;
  buf[i++] = status->EDID >> 16;
  buf[i++] = status->EDID >> 24;
  buf[i++] = status->CurPage;
  buf[i++] = status->PageMark;
  buf[i++] = status->Page1Mark;
  buf[i++] = status->Page2Mark;
  buf[i++] = status->Page3Mark;
  buf[i++] = status->Page4Mark;
  buf[i++] = status->ScreenType;
  buf[i++] = status->SignalQuality;
  buf[i++] = status->Voltage;
  buf[i++] = status->Voltage >> 8;
  buf[i++] = status->Temperature;
  buf[i++] = status->Temperature >> 8;
  buf[i++] = status->Factory;
  buf[i++] = status->TimeoutCount;
  buf[i++] = status->LEDMark;
  buf[i++] = status->NFCMark;
  buf[i++] = status->Version;
  buf[i++] = status->BlackList;
  buf[i++] = status->BlackList >> 8;
  buf[i++] = status->BlackList >> 16;
  buf[i++] = status->BlackList >> 24;
  buf[i++] = status->Belong;
  if (status->StartTime) {
    sscanf(status->StartTime, "%d-%d-%d %d:%d", &v1, &v2, &v3, &v4, &v5);
    buf[i++] = v1;
    buf[i++] = v2;
    buf[i++] = v3;
    buf[i++] = v4;
    buf[i++] = v5;
  } else {
    memset(buf + i, 0, 5);
    i += 5;
  }
  if (status->EndTime) {
    sscanf(status->EndTime, "%d-%d-%d %d:%d", &v1, &v2, &v3, &v4, &v5);
    buf[i++] = v1;
    buf[i++] = v2;
    buf[i++] = v3;
    buf[i++] = v4;
    buf[i++] = v5;
  } else {
    memset(buf + i, 0, 5);
    i += 5;
  }
  buf[i++] = status->GoodsMark;
  buf[i++] = status->DateType;
  if (status->LastTime) {
    sscanf(status->LastTime, "%d-%d-%d %d:%d", &v1, &v2, &v3, &v4, &v5);
		buf[i++] = v1;
		buf[i++] = v2;
		buf[i++] = v3;
		buf[i++] = v4;
		buf[i++] = v5;
  } else {
    memset(buf + i, 0, 5);
    i += 5;
  }
  buf[i++] = status->Back1;
  buf[i++] = status->Back2;
  buf[i++] = status->Back3;
  buf[i++] = status->Back4;

  int flen = i - pc - 2;  //len 不包含长度字段本身
  buf[pc] = flen;
  buf[pc+1] = flen >> 8;
  buf[i++] = 0xef;
  buf[i++] = 0xef;

  prop->index = i;

  /********************过期未更新价签加入删除列表*************************/
  time_t _now = time(NULL);
  if (_now - status->time > MAX_EXPIRED_TIME) {
    expired_status.edid[expired_status.idx++ % MAX_SCAN_NUM] = status->EDID;
  }

  return 0;
}

int read_whitelist(void *opaque, void *data) {
  WhiteList *wl = data;
  struct frame_prop *prop = opaque;
  uint8_t *buf = prop->frame;
  int i = prop->index;

  if (prop->task_num == MAX_NUM_OF_JQ) return 1;
  prop->last_edid = wl->EDID;
  if (wl->Flag) {
    prop->task_num ++;
    buf[i++] = wl->EDID;
    buf[i++] = wl->EDID >> 8;
    buf[i++] = wl->EDID >> 16;
    buf[i++] = wl->EDID >> 24;
    prop->index = i;
  }

  return 0;
}

int query_x_table(Cmd0x53 *cmd, Response0x53 *resp_0x53, uint32_t *last_edid) {
  int i = 0;
  System *sysconf = NULL;
  struct frame_prop prop;
  int v1, v2, v3, v4, v5, v6;

  if (cmd->table != 'D') {
    // table num[u16] size[u16] id data...
    prop.frame = resp_0x53->data;
    prop.index = 0;
    prop.task_num = 0;
    prop.last_edid = *last_edid;

    switch (cmd->table) {
      case 'A':
        traverse_ed_status(&prop.last_edid, makeup_a_table, &prop);
        break;
      case 'C':
        prop.frame[i++] = 0xee;
        prop.frame[i++] = 0xee;
        prop.frame[i++] = 0;
        prop.frame[i++] = 0;
        prop.frame += i;
        traverse_white_list(&prop.last_edid, read_whitelist, &prop);
        prop.frame[prop.index++] = 0xef;
        prop.frame[prop.index++] = 0xef;
        resp_0x53->data[2] = (uint8_t)prop.task_num;
        resp_0x53->data[3] = (uint8_t)(prop.task_num >> 8);
        break;
      default:
        return 0;
    }
    resp_0x53->num = prop.task_num;
    *last_edid = prop.last_edid;
    if (prop.task_num != MAX_NUM_OF_JQ)
      *last_edid = 0;
    return prop.index;
  } else {
    resp_0x53->num = 1;
    uint8_t *buf = resp_0x53->data;
    sysconf = get_system_conf();
    buf[i++] = 0xee;
    buf[i++] = 0xee;
    int pc = i;
    buf[i++] = 0;
    buf[i++] = 0;
    buf[i++] = sysconf->Type;
    if (sysconf->IPAddr) {
      sscanf(sysconf->IPAddr, "%d.%d.%d.%d", &v1, &v2, &v3, &v4);
      buf[i++] = v1;
      buf[i++] = v2;
      buf[i++] = v3;
      buf[i++] = v4;
    } else {
      memset(buf + i, 0, 4);
      i += 4;
    }
    if (sysconf->NetMask) {
      sscanf(sysconf->NetMask, "%d.%d.%d.%d", &v1, &v2, &v3, &v4);
      buf[i++] = v1;
      buf[i++] = v2;
      buf[i++] = v3;
      buf[i++] = v4;
    } else {
      memset(buf + i, 0, 4);
      i += 4;
    }
    if (sysconf->GateWay) {
      sscanf(sysconf->GateWay, "%d.%d.%d.%d", &v1, &v2, &v3, &v4);
      buf[i++] = v1;
      buf[i++] = v2;
      buf[i++] = v3;
      buf[i++] = v4;
    } else {
      memset(buf + i, 0, 4);
      i += 4;
    }
    if (sysconf->MAC) {
      sscanf(sysconf->MAC, "%02x:%02x:%02x:%02x:%02x:%02x", &v1, &v2, &v3, &v4, &v5, &v6);
      buf[i++] = v1;
      buf[i++] = v2;
      buf[i++] = v3;
      buf[i++] = v4;
      buf[i++] = v5;
      buf[i++] = v6;
    } else {
      //get_mac(buf + i, NET_DEV_NAME);  数据库加载时总是已经初始化过
      memset(buf + i, 0, 6);
      i += 6;
    }

    if (sysconf->DestIP1) {   // IF have IP
      /*
      size_t url_len = strlen(sysconf->DestIP1);
      if (url_len > URL_STR_MAX_LEN) url_len = URL_STR_MAX_LEN;
      strncpy((char*)(buf + i), (const char *)sysconf->DestIP1, url_len);
      i += url_len; //没有包含尾部的 0
      */
        sscanf(sysconf->DestIP1, "%d.%d.%d.%d", &v1, &v2, &v3, &v4);
        buf[i++] = v1;
        buf[i++] = v2;
        buf[i++] = v3;
        buf[i++] = v4;
    }
    else{
        memset(buf + i, 0, 4);
        i += 4;
    }
    //buf[i++] = '\0';
    buf[i++] = sysconf->DestPort1;
    buf[i++] = sysconf->DestPort1 >> 8;

    if (sysconf->DestIP2) {
    	/*
      size_t url_len = strlen(sysconf->DestIP2);
      if (url_len > URL_STR_MAX_LEN) url_len = URL_STR_MAX_LEN;
      strncpy((char*)(buf + i), (const char *)sysconf->DestIP2, url_len);
      i += url_len; //没有包含尾部的 0   */
        sscanf(sysconf->DestIP2, "%d.%d.%d.%d", &v1, &v2, &v3, &v4);
        buf[i++] = v1;
        buf[i++] = v2;
        buf[i++] = v3;
        buf[i++] = v4;
    }
    else{
        memset(buf + i, 0, 4);
        i += 4;
    }

    //buf[i++] = '\0';

    buf[i++] = sysconf->DestPort2;
    buf[i++] = sysconf->DestPort2 >> 8;

    buf[i++] = sysconf->Mark;
    buf[i++] = sysconf->Num;
    buf[i++] = sysconf->SubPicSize;
    buf[i++] = sysconf->SubPicSize >> 8;
    buf[i++] = sysconf->StationVer;
    buf[i++] = sysconf->BACK1;
    buf[i++] = sysconf->BACK2;
    buf[i++] = sysconf->BACK3;
    buf[i++] = sysconf->Ant1Freq;
    buf[i++] = sysconf->Ant1Freq>> 8;
    buf[i++] = sysconf->Ant2Freq;
    buf[i++] = sysconf->Ant2Freq>> 8;
    buf[i++] = sysconf->Ant3Freq;
    buf[i++] = sysconf->Ant3Freq>> 8;
    buf[i++] = sysconf->Ant4Freq;
    buf[i++] = sysconf->Ant4Freq>> 8;
    buf[i++] = sysconf->Ant1Args;
    buf[i++] = sysconf->Ant1Args >> 8;
    buf[i++] = sysconf->Ant1Args >> 16;
    buf[i++] = sysconf->Ant1Args >> 24;
    buf[i++] = sysconf->Ant2Args;
    buf[i++] = sysconf->Ant2Args >> 8;
    buf[i++] = sysconf->Ant2Args >> 16;
    buf[i++] = sysconf->Ant2Args >> 24;
    buf[i++] = sysconf->Ant3Args;
    buf[i++] = sysconf->Ant3Args >> 8;
    buf[i++] = sysconf->Ant3Args >> 16;
    buf[i++] = sysconf->Ant3Args >> 24;
    buf[i++] = sysconf->Ant4Args;
    buf[i++] = sysconf->Ant4Args >> 8;
    buf[i++] = sysconf->Ant4Args >> 16;
    buf[i++] = sysconf->Ant4Args >> 24;
    buf[i++] = sysconf->LoginCtl;
    if (sysconf->Username) {

      size_t url_len = strlen(sysconf->Username);
      if (url_len > URL_STR_MAX_LEN) url_len = URL_STR_MAX_LEN;
      strncpy((char*)(buf + i), (const char *)sysconf->Username, url_len);
      i += url_len; //没有包含尾部的 0


    }

    buf[i++] = '\0';

    if (sysconf->Passwd) {

      size_t url_len = strlen(sysconf->Passwd);
      if (url_len > URL_STR_MAX_LEN) url_len = URL_STR_MAX_LEN;
      strncpy((char*)(buf + i), (const char *)sysconf->Passwd, url_len);
      i += url_len; //没有包含尾部的 0

    }

    buf[i++] = '\0';

    if (sysconf->DestURL) {
      int x_url_len = strlen((const char*)sysconf->DestURL);
      if (x_url_len > URL_STR_MAX_LEN) x_url_len = URL_STR_MAX_LEN;
      strncpy((char*)(buf + i), (const char *)sysconf->DestURL, x_url_len);
      i += x_url_len; //没有包含尾部的 0
    }

    int flen = i - pc - 2;
    buf[pc] = flen;
    buf[pc + 1] = flen >> 8;
    buf[i++] = 0xef;
    buf[i++] = 0xef;
  }

  return i;
}

UpPacket *analysis_tcp_data(u8_t *data, unsigned int size, TcpContext *TcpCtx) {
  TcpHeader *header = (TcpHeader*)data;

  MEM_CHECK_RPTR(TcpCtx);
  MEM_CHECK_RPTR(TcpCtx->up_packet);

  if (header->fun_code == TASK_PIC_CODE) { // 局部图任务， 长度四个字节
    return analysis_JBT(data, size, TcpCtx);
  }
  unsigned int crc_index = sizeof(TcpHeader) + header->len;

  if (crc_index > size) {
    logger_debug("len error! %d , %d", crc_index, size);
    return NULL;
  }

  CRC_TYPE crc1 = MBCRC16(data, crc_index);
  CRC_TYPE crc2 = *(CRC_TYPE*)(data + crc_index);

  if (crc1 != crc2) {
    logger_debug("Expected crc = %hx, but got %hx.", crc1, crc2);
    return NULL;
  }

  TcpCtx->up_packet->len = 0;
  ResponseHead *resp_head = (ResponseHead*)TcpCtx->up_packet->data;
  resp_head->head = 0xfafa;
  resp_head->fun_code = header->fun_code;
  switch (header->fun_code) {
    case SET_SYS_CODE:  //参数设置
    {
      Cmd0x51 *cmd = (Cmd0x51*)(data + sizeof(TcpHeader));
      rwlock_wrlock();
      int ret = update_d_table(cmd, header->len - sizeof(Cmd0x51));
      rwlock_unlock();
      if (0 > ret) {
        logger_warning("update [d] table error!");
      }
      Response0x51 *resp_0x51 = (Response0x51*)resp_head->data;
      resp_0x51->status = ret;
      resp_head->len = sizeof(Response0x51);
    } break;
    case SET_WHITE_LIST_CODE: {
      Cmd0x52 *cmd = (Cmd0x52*)(data + sizeof(TcpHeader));
      rwlock_wrlock();
      if (cmd->black == 0xff ) {
        clear_white_list();
      } else {
        for (int i = 0; i < cmd->num; i++) {
          if (0 != update_white_list(cmd->EDID[i], cmd->black)) break;
        }
      }
      rwlock_unlock();
      resp_head->len = sizeof(Response0x52);
      Response0x52 *resp_0x52 = (Response0x52*)resp_head->data;
      resp_0x52->num = cmd->num;
    } break;
    case QUERY_SYS_CODE:  //参数查询 A/C/D 表
    {
      Cmd0x53 *cmd = (Cmd0x53*)(data + sizeof(TcpHeader));
      Response0x53 *resp_0x53 = (Response0x53*)resp_head->data;
      resp_0x53->table = cmd->table;
      resp_0x53->num = 0;
      rwlock_rdlock();
      int ret = query_x_table(cmd, resp_0x53, &TcpCtx->last_edid);
      rwlock_unlock();
      if (expired_status.idx) {
        // 在查询过程中删除过期价签
        rwlock_wrlock();
        for (int i = 0; i < MAX_SCAN_NUM; i++) {
          if (!expired_status.edid[i]) break;
          delete_status(expired_status.edid[i]);
          //2019-06-21 dj Del Task
          delete_task(expired_status.edid[i]);
        }
        rwlock_unlock();
        memset(&expired_status, 0, sizeof(expired_status));
      }
      if (!ret) {
        return NULL;
      }
      resp_head->len = sizeof(Response0x53) + ret;
    } break;
    case TASK_LED_CODE: {
      Cmd0x54 *cmd = (Cmd0x54*)(data + sizeof(TcpHeader));
      Response0x5X *resp = (Response0x5X*)resp_head->data;
      LEDData *dat = cmd->LED;
      rwlock_wrlock();
      for (int i = 0; i < cmd->num; i++) {
        if ( 0 != update_led_table(dat)) break;
        resp->EDID[i] = dat->EDID;
        dat ++;
      }
      rwlock_unlock();
      resp->num = cmd->num;
      resp_head->len = sizeof(Response0x5X) + resp->num * 4;
    } break;
    case TASK_NFC_CODE: {
      Cmd0x55 *cmd = (Cmd0x55*)(data + sizeof(TcpHeader));
      Response0x5X *resp = (Response0x5X*)resp_head->data;
      NFCData *dat = cmd->NFC;
      rwlock_wrlock();
      for (int i = 0; i < cmd->num; i++) {
        if (0 != update_nfc_table(dat)) break;
        resp->EDID[i] = dat->EDID;
        dat = (NFCData*)((uint8_t*)dat + dat->Len + sizeof(NFCData));
      }
      rwlock_unlock();
      resp->num = cmd->num;
      resp_head->len = sizeof(Response0x5X) + resp->num * 4;
    } break;
    case TASK_CHANGEPAGE_CODE: {
      Cmd0x56 *cmd = (Cmd0x56*)(data + sizeof(TcpHeader));
      Response0x5X *resp = (Response0x5X*)resp_head->data;
      PageData *dat = cmd->page;
      rwlock_wrlock();
      for (int i = 0; i < cmd->num; i++) {
        if (0 != update_qieye_table(dat->EDID, dat->PageMark)) break;
        resp->EDID[i] = dat->EDID;
        dat ++;
      }
      rwlock_unlock();
      resp->num = cmd->num;
      resp_head->len = sizeof(Response0x5X) + resp->num * 4;
    } break;
    case DELETE_TASK_CODE:
    case DELETE_STATUS_CODE: {
      Cmd0x59 *cmd = (Cmd0x59*)(data + sizeof(TcpHeader));
      Response0x5X *resp = (Response0x5X*)resp_head->data;
      rwlock_wrlock();
      if (cmd->num == 1 && cmd->EDID[0] == 0xffffffff) {
        if (header->fun_code == DELETE_TASK_CODE) delete_task(0);
        else delete_status(0);
        resp->EDID[0] = cmd->EDID[0];
      } else {
        for (int i = 0; i < cmd->num; i++) {
          if (header->fun_code == DELETE_TASK_CODE) delete_task(cmd->EDID[i]);
          else delete_status(cmd->EDID[i]);
          resp->EDID[i] = cmd->EDID[i];
        }
      }
      rwlock_unlock();
      resp->num = cmd->num;
      resp_head->len = sizeof(Response0x5X) + resp->num * 4;
    } break;
    case QUERY_STATUS_CODE: { // 指定EDID 查询 A表
      resp_head->fun_code = QUERY_SYS_CODE;
      Cmd0x5A *cmd = (Cmd0x5A*)(data + sizeof(TcpHeader));
      // table num[u16] size[u16] id data...
      Response0x53 *resp = (Response0x53*)resp_head->data;
      resp->table = 'A';
      resp->num = 0;
      struct frame_prop prop;
      prop.frame = resp->data;
      prop.index = 0;
      prop.task_num = 0;
      prop.last_edid = 0;
      cmd->num = cmd->num > MAX_NUM_OF_JQ?MAX_NUM_OF_JQ:cmd->num;
      rwlock_rdlock();
      for (int i = 0; i < cmd->num; i++) {
        EDStatus *status = get_ed_status(cmd->EDID[i]);
        if (status) makeup_a_table(&prop, status);
      }
      rwlock_unlock();

      resp->num = prop.task_num;
      resp_head->len = sizeof(Response0x53) + prop.index;

      if (expired_status.idx) {
        // 在查询过程中删除过期价签
        rwlock_wrlock();
        for (int i = 0; i < MAX_SCAN_NUM; i++) {
          if (!expired_status.edid[i]) break;
          delete_status(expired_status.edid[i]);
        }
        rwlock_unlock();
        memset(&expired_status, 0, sizeof(expired_status));
      }
    } break;
    case QUERY_TASK_CODE:  //查询任务
    {
      uint16_t *num = (uint16_t*)resp_head->data;
      struct frame_prop prop;
      prop.frame = resp_head->data + sizeof(uint16_t);
      prop.index = 0;
      prop.task_num = 0;
      rwlock_rdlock();
      traverse_ed_task(&TcpCtx->last_edid, query_task, &prop);
      rwlock_unlock();
      TcpCtx->last_edid = prop.last_edid;
      if (prop.task_num != MAX_NUM_OF_TASK)
        TcpCtx->last_edid = 0;

      *num = prop.task_num;
      resp_head->len = sizeof(uint16_t) + prop.index;
    } break;
    case SYNC_TIME_CODE: //时间同步
    {
      Cmd0x7A *cmd = (Cmd0x7A*)(data + sizeof(TcpHeader));
      Response0x7A *resp = (Response0x7A*)resp_head->data;
      SysTime *tm = (SysTime*)cmd;
      set_sys_time(*tm);
      get_sys_time((SysTime*)resp);
      resp_head->len = sizeof(Response0x7A);
    } break;
    default:
      return NULL;
  }

  CRC_TYPE *crc = (CRC_TYPE*)(resp_head->data + resp_head->len);
  *crc = MBCRC16(&resp_head->fun_code, resp_head->len + sizeof(ResponseHead) - 2);
  END_TYPE *end_code = (END_TYPE*)(resp_head->data + resp_head->len + 2);
  *end_code = 0xfbfb;
  TcpCtx->up_packet->len = sizeof(ResponseHead) + resp_head->len + sizeof(*crc) + sizeof(*end_code);

  if (TcpCtx->up_packet-> len > TcpCtx->up_packet->size) {
    logger_error("Memory overflow, system failure.");
    MEM_CHECK_RPTR(NULL);
  }
  return TcpCtx->up_packet;
}

