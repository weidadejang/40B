
#include "log.h"
#include "list.h"
#include "crc.h"
#include "main.h"
#include "serial.h"
#include "xspi.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>

#define PACKET_MAX_SIZE (55)
#define MAX_LIST_NUM (500)
#define PIC_SLICE_SIZE (4096)
#define CRC_TYPE uint16_t
/*
 * 0x62 : sizeof(SerialHeader2) + sizeof(CRC_TYPE) +
 *        sizeof(Response0x62) * MAX_LIST_NUM
 * 0x67 : sizeof(SerialHeader2) + sizeof(CRC_TYPE) +
 *        sizeof(Response0x67) + PIC_SLICE_SIZE
 * Seems 5k Bytes is enough
 */
#define PROTOBUF_MAX_SIZE (5 * 1024)
// struct timeval start_time, end_time;

struct Status0x62 {
  uint32_t last_0x62_edid;
  uint32_t wire_belong;
  uint16_t req_num;
};

struct ProtoBuf {
  uint8_t *data;
  size_t size;      // max size
  size_t len;       // cur size
};

typedef struct SerialDev {
  const char *name;
  FDSET fd;
  pthread_t tid;
  // per-ed tasks
  list *ed_list;
  // 0x62 task status
  struct Status0x62 status_0x62;
  // output protocol buffer
  struct ProtoBuf proto_buf;
} SerialDev;

typedef struct SerialHeader {
  uint8_t fun_code;
  uint8_t len;
} __PACKED SerialHeader;

typedef struct SerialHeader2 {
  uint8_t fun_code;
  uint16_t len;
} __PACKED SerialHeader2;

typedef struct Request0x60 {
  uint32_t EDID;
  uint16_t max_task_num;
  uint8_t back1;
  uint8_t back2;
  uint8_t wire_belong;
} __PACKED Request0x60;

typedef struct Request0x62 {
  uint16_t num;
  uint8_t wire_belong;
} __PACKED Request0x62;

typedef struct Request0x6D {
  uint32_t EDID;
  uint8_t cur_page;
  uint8_t page_mark;
  uint8_t page1_mark;
  uint8_t page2_mark;
  uint8_t page3_mark;
  uint8_t page4_mark;
  uint8_t screen_type;
  uint8_t signal_strength;
  uint16_t voltage;
  uint16_t tempur;
  uint8_t fact;
  uint8_t timeout;
  uint8_t led_mark;
  uint8_t nfc_mark;
  uint8_t ver;
  uint32_t store_id;
  uint8_t wire_belog;
  uint8_t start_year;
  uint8_t start_month;
  uint8_t start_day;
  uint8_t start_hour;
  uint8_t start_min;
  uint8_t end_year;
  uint8_t end_month;
  uint8_t end_day;
  uint8_t end_hour;
  uint8_t end_min;
  uint8_t goods_mark;
  uint8_t time_mark;
  uint8_t reserved[4];
  uint8_t wire_channel;
  uint16_t wire_update_span;
  uint32_t wire_cycle_time;
  uint16_t wire_ed_num;
} __PACKED Request0x6D;

typedef struct Request0x67 {
  uint32_t EDID;
  uint16_t cur_pkt;
  uint8_t back[2];
  uint8_t wire_belong;
} __PACKED Request0x67;

typedef struct Response0x62 {
  uint32_t EDID;
  uint8_t funcode;
  uint8_t pkt_num;
} __PACKED Response0x62;


typedef struct Response0x6B {
  uint8_t store_mark;
  uint8_t ap_type;
  uint8_t sotre_ap_count;
  uint8_t reserved[3];
} __PACKED Response0x6B;

typedef struct Response0x6D {
  uint32_t EDID;
  uint8_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t min;
  uint8_t sec;                        //dongjing add 2019-12-06
  Response0x6B global_par;
  uint8_t powersave;
  uint16_t start_time;
  uint32_t duration;
} __PACKED Response0x6D;

typedef struct Response0x67 {
  uint32_t EDID;
  uint16_t pkt_num;
  uint16_t pkt_idx;
  uint8_t pic_slice[0];
} __PACKED Response0x67;

typedef struct Response0x66 {
  uint32_t EDID;
  uint8_t page_mark;
} __PACKED Response0x66;

typedef struct Response0x65 {
  uint32_t EDID;
  uint8_t black_mark;
} __PACKED Response0x65;


typedef struct Response0x64 {
  uint32_t EDID;
  uint8_t LedMark;
  uint8_t Led1Mark;
  uint8_t Led2Mark;
  uint8_t Led3Mark;
  uint8_t Led4Mark;
} __PACKED Response0x64;

typedef struct Response0x63 {
  uint32_t EDID;
  uint8_t nfc_mark;
  char nfc_data[0];
} __PACKED Response0x63;

/* 记录某个ED的任务分页状态 */
typedef struct EDProperty {
  uint32_t EDID;
  struct PicTask {
    void *data;
    size_t len;
    uint16_t cur_slice;
    uint16_t slice_num;
  } pic_task;
} EDProperty;

static void *ThreadSerial(void *arg);
static int Send2Serial(SerialDev *dev);

static int process_0x60(SerialDev*, void *, size_t);
static int process_0x62(SerialDev*, void *, size_t);
static int process_0x67(SerialDev*, void *, size_t);
static int process_0x6D(SerialDev*, void *, size_t);
static int process_0x6F(SerialDev*, void *, size_t);

static int check_0x6B(uint32_t EDID);
static int check_0x6F(uint32_t EDID);
static int check_0x67(uint32_t EDID);
static int check_0x66(uint32_t EDID);
static int check_0x65(uint32_t EDID);
static int check_0x64(uint32_t EDID);
static int check_0x63(uint32_t EDID);

static int send_0x6B(SerialDev*, uint32_t);
static int send_0x6F(SerialDev*, uint32_t);
static int send_0x67(SerialDev*, uint32_t);
static int send_0x66(SerialDev*, uint32_t);
static int send_0x65(SerialDev*, uint32_t);
static int send_0x64(SerialDev*, uint32_t);
static int send_0x63(SerialDev*, uint32_t);
static int send_0x6D(SerialDev*, uint32_t);
static int send_0x62(SerialDev*);

static int pack_0x62(void *opaque, void *data);

static int ed_match(void *, void *);
static void ed_free(void *);
static struct EDProperty *ed_new(SerialDev *, uint32_t);

// 所有支持的串口发上来的命令
static const struct SerialRequest {
  uint8_t fun_code;
  int (*request_proc)(SerialDev*, void*, size_t);
} request_list[] = {
  {0x60, process_0x60}, // Query
  {0x62, process_0x62}, // Query
  {0x6D, process_0x6D}, // ED Status
  {0x6F, process_0x6F}, // Task
  {0x67, process_0x67}, // Pic T.B.C.
};

static const struct SerialResponse {
  uint8_t fun_code;
  int (*response_ready)(uint32_t);
  int (*response_send)(SerialDev*, uint32_t);
} response_list[] = {
  {0x67, check_0x67, send_0x67}, // Priority Highest
  {0x66, check_0x66, send_0x66},
  {0x65, check_0x65, send_0x65},
  {0x64, check_0x64, send_0x64},
  {0x63, check_0x63, send_0x63},
  {0x6B, check_0x6B, send_0x6B}, // Priority lowest
  {0x6F, check_0x6F, send_0x6F}, // Seems to be redundant, same as 0x6B
};

static int task_num = 0;
static int task_exit = 0;
static SerialDev *serial_devs;

int start_serial_tasks(int ndev, const char *devs[]) {
  int idx, ret;

  serial_devs = calloc(ndev, sizeof(SerialDev));
  MEM_CHECK_R_1(serial_devs);
  task_num = ndev;

  for (idx = 0; idx < ndev; idx++) {
    serial_devs[idx].ed_list = listCreate();
    serial_devs[idx].ed_list->match = ed_match;
    serial_devs[idx].ed_list->free = ed_free;
    serial_devs[idx].name = devs[idx];
    ret = pthread_create(&serial_devs[idx].tid, NULL, ThreadSerial,
                         &serial_devs[idx]);
    if (ret != 0) {
      logger_error("Fail to start thread");
      return ret;
    }
    serial_devs[idx].proto_buf.data = malloc(PROTOBUF_MAX_SIZE);
    serial_devs[idx].proto_buf.size = PROTOBUF_MAX_SIZE;
    serial_devs[idx].proto_buf.len = 0;
    MEM_CHECK_R_1(serial_devs[idx].proto_buf.data);
  }

  return 0;
}

void stop_serial_tasks() {
  int idx;

  task_exit = 1;
  for (idx = 0; idx < task_num; idx++) {
    pthread_join(serial_devs[idx].tid, NULL);
    listRelease(serial_devs[idx].ed_list);
    free(serial_devs[idx].proto_buf.data);
  }
  free(serial_devs);
}

static const struct SerialRequest* check_fun_code(uint8_t funcode) {
  unsigned int idx = 0;
  for (; idx < sizeof(request_list)/sizeof(request_list[0]); idx++)
    if (funcode == request_list[idx].fun_code)
      return request_list + idx;

  return NULL;
}

static int check_crc(uint8_t *data, size_t len) {
  CRC_TYPE crc1 = MBCRC16(data, len - sizeof(CRC_TYPE));
  CRC_TYPE crc2 = *(uint16_t*)(data + len - sizeof(CRC_TYPE));

  if (crc1 != crc2) {
    logger_debug("Expected crc = %hx, but got %hx.", crc1, crc2);
    return 0;
  }

  return 1;
}

void *ThreadSerial(void *arg) {
  SerialDev *dev = arg;
  uint16_t err_cnt = 0;
  uint8_t tmpbuf[PACKET_MAX_SIZE] = {0};
  int len = 0;
  SerialHeader *header;
  const struct SerialRequest *func;
  char *thread_name = strrchr(dev->name, '/');
  int reopen_try_cnt = 0;
  int last_time_of_reopen = 100;

  pthread_setspecific (tls_key_threadnr, thread_name + 1);
  logger_info("run...");

OpenSerial:
  // 重新打开串口时，串口号对应的设备可能已经改变，需要清空list
  listEmpty(dev->ed_list);
  do {
    dev->fd = serial_init(dev->name, "115200,8,1,n", 5, 1);
    if (dev->fd.comfd < 0) {
      logger_error("%s serial open failed.", dev->name);
      sleep(3);
    }
  } while(!task_exit && dev->fd.comfd < 0);

  while (!task_exit) {
    // gettimeofday(&start_time, NULL);
    len = SerialRec(dev->fd, tmpbuf, sizeof(tmpbuf), 10);
    if (len > 0) logger_hexbuf("SERIAL RX", tmpbuf, len);
    if (len < 0 || (unsigned int)len < sizeof(*header) + 2) {
      if (len == 0) err_cnt++;
      else err_cnt += 2;
      if (++ err_cnt > 600) {

    	logger_debug("---------->>>>reopen_try_cnt: %d, last_time_of_reopen: %d", reopen_try_cnt, last_time_of_reopen);
        logger_error("The serial port will be reopened.");
        err_cnt = 0;
        close_serial(&dev->fd);
        if (reopen_try_cnt ++ > 1000) {
          //system("reboot");
          logger_info("--------system will be reboot------------");
          restart = 1;
        }
        goto OpenSerial;
      }
      continue;
    }
    err_cnt = 0;
    if (last_time_of_reopen -- == 0) {
      last_time_of_reopen = 10;
      reopen_try_cnt = 0; // 重启串口如果能连续正常1000次
    }

    header = (SerialHeader *)tmpbuf;
    if (header->len > len) {
      logger_debug("Not all data received. expect: %d, get: %d", header->len, len);
      continue;
    }

    func = check_fun_code(header->fun_code);
    if (!func) {
      logger_debug("Unknown func code %hhx", header->fun_code);
      continue;
    }

    if (!check_crc(tmpbuf, len))
      continue;

    func->request_proc(dev, tmpbuf + sizeof(SerialHeader),
                       (size_t)len - sizeof(SerialHeader) - sizeof(CRC_TYPE));
  }

  logger_info("exit...");
  if (dev->fd.comfd > 0) close_serial(&dev->fd);

  return NULL;
}


/*------------------------------------------------------------------------------------------------
 *
 *------------------------------------------------------------------------------------------------ */
void *SPI10_Communication(void *arg){

	int fd = 0;


	UNUSED(arg);
	pthread_setspecific (tls_key_threadnr, "SPI10");

	OpenSPI:
	for(;;)
	{
		sleep(3);
		logger_info("SPI configration...");
		fd = SPI10_Configration();
		if(fd < 0){
		}
		else{
			break;
		}
	}


	while(1)
	{
		int i = SPI_Processing(fd);
		if(i < 0)
		{
			goto OpenSPI;
		}
		//logger_info("SPI configration...");
		sleep(1);
	}


	return NULL;
}

/*------------------------------------------------------------------------------------------------
 *
 *------------------------------------------------------------------------------------------------ */
void start_SPI10_task(void)
{
	  pthread_t pd;
	  pthread_create(&pd, NULL, SPI10_Communication, NULL);
}


int process_0x60(SerialDev *dev, void *data, size_t len) {
  Request0x60 *req = data;

  ASSERT(len == sizeof(*req));
  logger_debug("Query from %#x, capacity: %u", req->EDID, req->max_task_num);

  logger_info("request funcode:%#x", req->back1);
  // 一次处理一个任务？？？
  for(unsigned int idx = 0; idx < NELEMS(response_list); idx++) {
    rwlock_rdlock();
    int ret = response_list[idx].response_ready(req->EDID);
    rwlock_unlock();
    if ( ret > 0 && response_list[idx].fun_code == req->back1) //请求的功能码也对应
      return response_list[idx].response_send(dev, req->EDID);
  }

  // 没有任何任务
  return send_0x6B(dev, req->EDID);
}

int process_0x62(SerialDev *dev, void *data, size_t len) {
  Request0x62 *req = data;

  ASSERT(len == sizeof(*req));
  ASSERT(req->num != 0);

  dev->status_0x62.req_num = req->num > MAX_LIST_NUM ? MAX_LIST_NUM : req->num;
  dev->status_0x62.wire_belong = req->wire_belong;
  return send_0x62(dev);
}

int process_0x67(SerialDev *dev, void *data, size_t len) {
  Request0x67 *req = data;
  listNode *node = listSearchKey(dev->ed_list, &req->EDID);
  EDProperty *prop;

  ASSERT(len == sizeof(*req));
  ASSERT(node != NULL);

  prop = node->value;
  ASSERT(prop != NULL);
  // no pic task or error occurs on cur task
  if (!prop->pic_task.data || prop->pic_task.cur_slice != req->cur_pkt) {
    logger_debug("Serial packet lost. cur %d, but got %d.", prop->pic_task.cur_slice, req->cur_pkt);
    listDelNode(dev->ed_list, node);
    return -ENOENT;
  }

  // all data transfered
  if (prop->pic_task.cur_slice == prop->pic_task.slice_num - 1) {
    logger_debug("Pic transfer ending for %d", prop->EDID);
    listDelNode(dev->ed_list, node);
    return 0;
  }

  // Transfer next slice.
  prop->pic_task.cur_slice++;
  return send_0x67(dev, req->EDID);
}

int process_0x6D(SerialDev *dev, void *data, size_t len) {
  Request0x6D *req = data;
  EDStatus *status;
  WireStatus *ws;
  time_t t = time(NULL);
  struct tm res;
  int WireB = 0;

  ASSERT(len == sizeof(Request0x6D));
  ASSERT(req->EDID != 0);

  // logger_info("EDID:%#x", req->EDID);
  rwlock_wrlock();
  status = get_ed_status(req->EDID);
  if (!status) {
    logger_info("New edid %08x found on serial %s\n", req->EDID, dev->name);
    status = db_alloc_row("EDStatus");
    if (!status) {
      rwlock_unlock();
      kill(getpid(), SIGUSR1);
      logger_error("---------malloc failed--------");
      return -1;
    }
    status->EDID = req->EDID;
    db_cached_insert_row(db_handle, status);
  }

  status->CurPage = req->cur_page;
  // logger_debug("status->PageMark:%#x req->PageMark:%#x", status->PageMark, req->page_mark);
  status->PageMark = req->page_mark;
  status->Page1Mark = req->page1_mark;
  status->Page2Mark = req->page2_mark;
  status->Page3Mark = req->page3_mark;
  status->Page4Mark = req->page4_mark;
  status->ScreenType = req->screen_type;
  status->SignalQuality = req->signal_strength;
  status->Voltage = req->voltage;
  status->Temperature = req->tempur;
  status->Factory = req->fact;
  status->TimeoutCount = req->timeout;
  status->LEDMark = req->led_mark;
  status->NFCMark = req->nfc_mark;
  status->Version = req->ver;
  status->Blacklist = req->store_id;
  status->Belong = req->wire_belog;

  if (!status->StartTime)
    status->StartTime = calloc(1, 20);
  MEM_CHECK_R_1(status->StartTime);
  snprintf(status->StartTime, 20, "%d-%d-%d %d:%d", req->start_year,
          req->start_month, req->start_day, req->start_hour,
          req->start_min);
  if (!status->EndTime)
    status->EndTime = calloc(1, 20);
  MEM_CHECK_R_1(status->EndTime);
  snprintf(status->EndTime, 20, "%d-%d-%d %d:%d", req->end_year,
          req->end_month, req->end_day, req->end_hour,
          req->end_min);
  status->GoodsMark = req->goods_mark;
  status->DateType = req->time_mark;
  status->Back1 = req->reserved[0];
  status->Back2 = req->reserved[1];
  status->Back3 = req->reserved[2];
  status->Back4 = req->reserved[3];

  if (!status->LastTime)
    status->LastTime = calloc(1, 20);

  localtime_r(&t, &res);
  if(t > status->time)
  {
	  MEM_CHECK_R_1(status->LastTime);
	  snprintf(status->LastTime, 20, "%d-%d-%d %d:%d", res.tm_year - 100,
			  res.tm_mon + 1, res.tm_mday, res.tm_hour,
			  res.tm_min);
	  status->time = t;
	  // wire_belog must 1-4
	  WireB = req->wire_belog + 1;
	  ws = get_wire_status(WireB);
	  if (ws) {
		ws->Channel = req->wire_channel;
		ws->SpanTime = req->wire_update_span;
		ws->CycleTime = req->wire_cycle_time;
		ws->EDCount = req->wire_ed_num;
	  }
  }
  rwlock_unlock();

  return send_0x6D(dev, req->EDID);
}

int process_0x6F(SerialDev *dev, void *data, size_t len) {
  // Not to be supported.
  UNUSED(dev);
  UNUSED(data);
  UNUSED(len);
  return -EINVAL;
}

int check_0x6B(uint32_t EDID) {
  return 0 == EDID;
}

int check_0x67(uint32_t EDID) {
  EDTask *task = get_ed_task(EDID);
  EDStatus *status = get_ed_status(EDID);
  int ret = 0;

  ASSERT(EDID != 0);
  if (!task || !status) return 0;

  logger_debug("%08X check PageMark[1-5] status:task [%d:%d %d:%d %d:%d %d:%d %d:%d]", EDID, \
      status->PageMark, task->PageMark, status->Page1Mark, task->Page1Mark, \
      status->Page2Mark, task->Page2Mark, status->Page3Mark, task->Page3Mark, \
      status->Page4Mark, task->Page4Mark);
  if ((task->PageMark && status->PageMark != task->PageMark) ||
      (task->Page1Mark && status->Page1Mark != task->Page1Mark) ||
      (task->Page2Mark && status->Page2Mark != task->Page2Mark) ||
      (task->Page3Mark && status->Page3Mark != task->Page3Mark) ||
      (task->Page4Mark && status->Page4Mark != task->Page4Mark))
    ret = 1;

  logger_info("Task check:%s", ret?"true":"false");
  return ret;
}

int check_0x66(uint32_t EDID) {
  EDTask *task = get_ed_task(EDID);
  EDStatus *status = get_ed_status(EDID);
  int ret = 0;

  ASSERT(EDID != 0);
  if (!task || !status) return 0;

  // 常页 ！= 切页 或者 当前显示 ！= 切页，则发送切页任务
  if (task->PageChange && status->CurPage != task->PageChange)
    ret = 1;

  logger_info("Task check:%s", ret?"true":"false");
  return ret;
}

int check_0x65(uint32_t EDID) {
  EDTask *task = get_ed_task(EDID);
  EDStatus *status = get_ed_status(EDID);
  int ret = 0;

  ASSERT(EDID != 0);
  if (!task || !status) return 0;

  if (task->Black && !(task->Black == (uint8_t)status->Blacklist ||
      task->Black == (uint8_t)(status->Blacklist >> 8) ||
      task->Black == (uint8_t)(status->Blacklist >> 16) ||
      task->Black == (uint8_t)(status->Blacklist >> 24)))
    ret = 1;

  logger_info("Task check:%s", ret?"true":"false");
  return ret;
}

int check_0x64(uint32_t EDID) {
  EDTask *task = get_ed_task(EDID);
  EDStatus *status = get_ed_status(EDID);
  int ret = 0;

  ASSERT(EDID != 0);
  if (!task || !status) return 0;

  if (task->LedMark && status->LEDMark != task->LedMark)
    ret = 1;

  logger_info("Task check:%s", ret?"true":"false");
  return ret;
}

int check_0x63(uint32_t EDID) {
  EDTask *task = get_ed_task(EDID);
  EDStatus *status = get_ed_status(EDID);
  int ret = 0;

  ASSERT(EDID != 0);
  if (!task || !status) return 0;

  if (task->NFCMark && status->NFCMark != task->NFCMark)
    ret = 1;

  logger_info("Task check:%s", ret?"true":"false");
  return ret;
}

int check_0x6F(uint32_t EDID) {
  // Not to be supported.
  UNUSED(EDID);

  return 0;
}

int send_0x6B(SerialDev *dev, uint32_t EDID) {
  SerialHeader *header = (SerialHeader *)dev->proto_buf.data;
  Response0x6B *response = (Response0x6B *)(header + 1);
  CRC_TYPE *crc = (CRC_TYPE *)(response + 1);
  System *system;

  header->fun_code = 0x6B;
  dev->proto_buf.len = sizeof(*header);

  rwlock_rdlock();
  system = get_system_conf();
  response->store_mark = system->BlacklistMark;
  response->ap_type = system->Type;
  response->sotre_ap_count = system->Num;
  response->reserved[0] = system->BACK1;
  response->reserved[1] = system->BACK2;
  response->reserved[2] = system->BACK3;
  rwlock_unlock();
  dev->proto_buf.len += sizeof(*response);

  header->len = dev->proto_buf.len + sizeof(*crc);
  *crc = MBCRC16(dev->proto_buf.data, dev->proto_buf.len);
  dev->proto_buf.len += sizeof(*crc);

  UNUSED(EDID);
  return Send2Serial(dev);
}

int send_0x67(SerialDev *dev, uint32_t EDID) {
  SerialHeader2 *header = (SerialHeader2 *)dev->proto_buf.data;
  Response0x67 *response = (Response0x67 *)(header + 1);
  size_t pic_len;
  uint8_t cs = 0;
  EDProperty *prop;
  listNode *node;

  ASSERT(EDID != 0);

  header->fun_code = 0x67;
  dev->proto_buf.len = sizeof(*header);

  node = listSearchKey(dev->ed_list, &EDID);
  if (!node)
    prop = ed_new(dev, EDID);
  else
    prop = node->value;

  if (!prop->pic_task.data) {
    rwlock_rdlock();
    EDStatus *status = get_ed_status(EDID);
    EDTask *task = get_ed_task(EDID);
    struct Blob *blob = NULL;

    logger_debug("no current task exist. Start new task.\n");
    if(status == NULL || task == NULL) {
      rwlock_unlock();
      return -EINVAL;
    }

    logger_debug("%08X check PageMark[1-5] status:task [%d:%d %d:%d %d:%d %d:%d %d:%d]", EDID, \
        status->PageMark, task->PageMark, status->Page1Mark, task->Page1Mark, \
        status->Page2Mark, task->Page2Mark, status->Page3Mark, task->Page3Mark, \
        status->Page4Mark, task->Page4Mark);
    // logger_debug("%08x status->PageMark:%#x task->PageMark:%#x", EDID, status->PageMark, task->PageMark);
    if (task->PageMark && status->PageMark != task->PageMark)
      blob = &task->PagePic;
    else if (task->Page1Mark && status->Page1Mark != task->Page1Mark)
      blob = &task->Page1Pic;
    else if (task->Page2Mark && status->Page2Mark != task->Page2Mark)
      blob = &task->Page2Pic;
    else if (task->Page3Mark && status->Page3Mark != task->Page3Mark)
      blob = &task->Page3Pic;
    else if (task->Page4Mark && status->Page4Mark != task->Page4Mark)
      blob = &task->Page4Pic;
    else
      blob = NULL;

    if (blob == NULL) {
      rwlock_unlock();
      listDelNode(dev->ed_list, node);
      return -EINVAL;
    }
    prop->pic_task.len = blob->nLen;
    logger_debug("Picture size:%d",blob->nLen);
    prop->pic_task.data = malloc(prop->pic_task.len);

    if (!prop->pic_task.data || !blob->data) {
      rwlock_unlock();
      listDelNode(dev->ed_list, node);
      MEM_CHECK_R_1(NULL);
      return -EINVAL;
    }

    memcpy(prop->pic_task.data, blob->data, prop->pic_task.len);
    prop->pic_task.slice_num =
            (uint16_t)ceil((float)prop->pic_task.len / PIC_SLICE_SIZE);
    rwlock_unlock();
  } else
    prop = node->value;

  if (prop->pic_task.cur_slice == prop->pic_task.slice_num - 1) // last slice
    pic_len = prop->pic_task.len - PIC_SLICE_SIZE * prop->pic_task.cur_slice;
  else
    pic_len = PIC_SLICE_SIZE;
  dev->proto_buf.len += (sizeof(*response) + pic_len);

  response->EDID = EDID;
  response->pkt_num = prop->pic_task.slice_num;
  response->pkt_idx = prop->pic_task.cur_slice;

  logger_debug("EDID:%08X pkt_num:%d pkt_idx:%d", EDID, response->pkt_num,
               response->pkt_idx);
  memcpy(response->pic_slice,
         prop->pic_task.data + PIC_SLICE_SIZE * prop->pic_task.cur_slice,
         pic_len);

  header->len = dev->proto_buf.len + 1;
  for (unsigned int i = 0; i < dev->proto_buf.len; i ++)
    cs ^= dev->proto_buf.data[i];
  dev->proto_buf.data[dev->proto_buf.len++] = cs;

  return Send2Serial(dev);
}

int send_0x66(SerialDev *dev, uint32_t EDID) {
  SerialHeader2 *header = (SerialHeader2 *)dev->proto_buf.data;
  uint16_t *ed_num = (uint16_t *)(header + 1);
  Response0x66 *response = (Response0x66 *)(ed_num + 1);
  CRC_TYPE *crc = (CRC_TYPE *)(response + 1);

  ASSERT(EDID != 0);

  header->fun_code = 0x66;
  dev->proto_buf.len = sizeof(*header);

  *ed_num = 0;
  dev->proto_buf.len += sizeof(*ed_num);

  rwlock_rdlock();
  EDTask *task = get_ed_task(EDID);
  if (!task) {
    rwlock_unlock();
    return -1;
  }
  response->EDID = task->EDID;
  response->page_mark = task->PageChange;
  rwlock_unlock();
  dev->proto_buf.len += sizeof(*response);

  *ed_num = 1;
  header->len = dev->proto_buf.len + sizeof(*crc);
  *crc = MBCRC16(dev->proto_buf.data, dev->proto_buf.len);
  dev->proto_buf.len += sizeof(*crc);

  // logger_debug("EDID:%08X", EDID);
  return Send2Serial(dev);
}

int send_0x65(SerialDev *dev, uint32_t EDID) {
  SerialHeader2 *header = (SerialHeader2 *)dev->proto_buf.data;
  uint16_t *ed_num = (uint16_t *)(header + 1);
  Response0x65 *response = (Response0x65 *)(ed_num + 1);
  CRC_TYPE *crc = (CRC_TYPE *)(response + 1);

  ASSERT(EDID != 0);

  header->fun_code = 0x65;
  dev->proto_buf.len = sizeof(*header);

  *ed_num = 0;
  dev->proto_buf.len += sizeof(*ed_num);

  rwlock_rdlock();
  EDTask *task = get_ed_task(EDID);
  if (!task) {
    rwlock_unlock();
    return -1;
  }
  response->EDID = task->EDID;
  response->black_mark= task->Black;
  rwlock_unlock();
  dev->proto_buf.len += sizeof(*response);

  *ed_num = 1;
  header->len = dev->proto_buf.len + sizeof(*crc);
  *crc = MBCRC16(dev->proto_buf.data, dev->proto_buf.len);
  dev->proto_buf.len += sizeof(*crc);

  // logger_debug("EDID:%08X", EDID);
  return Send2Serial(dev);
}

int send_0x64(SerialDev *dev, uint32_t EDID) {
  SerialHeader2 *header = (SerialHeader2 *)dev->proto_buf.data;
  uint16_t *ed_num = (uint16_t *)(header + 1);
  Response0x64 *response = (Response0x64 *)(ed_num + 1);
  CRC_TYPE *crc = (CRC_TYPE *)(response + 1);

  ASSERT(EDID != 0);

  header->fun_code = 0x64;
  dev->proto_buf.len = sizeof(*header);

  *ed_num = 0;
  dev->proto_buf.len += sizeof(*ed_num);

  rwlock_rdlock();
  EDTask *task = get_ed_task(EDID);
  if (!task) {
    rwlock_unlock();
    return -1;
  }
  response->EDID = task->EDID;
  response->LedMark = task->LedMark;
  response->Led1Mark = task->Led1Mark;
  response->Led2Mark = task->Led2Mark;
  response->Led3Mark = task->Led3Mark;
  response->Led4Mark = task->Led4Mark;
  rwlock_unlock();
  dev->proto_buf.len += sizeof(*response);

  *ed_num = 1;
  header->len = dev->proto_buf.len + sizeof(*crc);
  *crc = MBCRC16(dev->proto_buf.data, dev->proto_buf.len);
  dev->proto_buf.len += sizeof(*crc);

  // logger_debug("EDID:%08X", EDID);
  return Send2Serial(dev);
}

int send_0x63(SerialDev *dev, uint32_t EDID) {
  SerialHeader2 *header = (SerialHeader2 *)dev->proto_buf.data;
  uint16_t *ed_num = (uint16_t *)(header + 1);
  Response0x63 *response = (Response0x63 *)(ed_num + 1);
  CRC_TYPE *crc;

  ASSERT(EDID != 0);

  header->fun_code = 0x63;
  dev->proto_buf.len = sizeof(*header);

  *ed_num = 0;
  dev->proto_buf.len += sizeof(*ed_num);

  rwlock_rdlock();
  EDTask *task = get_ed_task(EDID);
  if (!task) {
    rwlock_unlock();
    return -1;
  }
  response->EDID = task->EDID;
  response->nfc_mark = task->NFCMark;
  strcpy(response->nfc_data, task->NFCData);
  rwlock_unlock();
  dev->proto_buf.len += sizeof(*response) + strlen(response->nfc_data);

  *ed_num = 1;
  header->len = dev->proto_buf.len + sizeof(*crc);
  crc = (CRC_TYPE *)(dev->proto_buf.data + dev->proto_buf.len);
  *crc = MBCRC16(dev->proto_buf.data, dev->proto_buf.len);
  dev->proto_buf.len += sizeof(*crc);

  // logger_debug("EDID:%08X", EDID);
  return Send2Serial(dev);
}

int send_0x6D(SerialDev *dev, uint32_t EDID) {
  SerialHeader *header = (SerialHeader *)dev->proto_buf.data;
  Response0x6D *response = (Response0x6D *)(header + 1);
  CRC_TYPE *crc = (CRC_TYPE *)(response + 1);
  System *system;
  time_t t = time(NULL);
  struct tm res;

  ASSERT(EDID != 0);

  header->fun_code = 0x6d;
  dev->proto_buf.len = sizeof(*header);

  localtime_r(&t, &res);
  response->EDID = EDID;
  response->year = res.tm_year - 100; // 0 - 255 {2000-2255}
  response->month = res.tm_mon + 1;   // 1 - 12
  response->day = res.tm_mday;
  response->hour = res.tm_hour;
  response->min = res.tm_min;
  response->sec = res.tm_sec;        //dongjing add 2019-12-06

  rwlock_rdlock();
  system = get_system_conf();
  response->global_par.store_mark = system->BlacklistMark;
  response->global_par.ap_type = system->Type;
  response->global_par.sotre_ap_count = system->Num;
  response->global_par.reserved[0] = system->BACK1;
  response->global_par.reserved[1] = system->BACK2;
  response->global_par.reserved[2] = system->BACK3;
  response->powersave = system->LowPower;
  response->start_time = system->StartTime;
  response->duration = system->Duration;
  rwlock_unlock();
  dev->proto_buf.len += sizeof(*response);

  header->len = dev->proto_buf.len + sizeof(*crc);
  *crc = MBCRC16(dev->proto_buf.data, dev->proto_buf.len);
  dev->proto_buf.len += sizeof(*crc);

  return Send2Serial(dev);
}

int send_0x62(SerialDev *dev) {
  SerialHeader2 *header = (SerialHeader2 *)dev->proto_buf.data;
  CRC_TYPE *crc = NULL;
  int num;

  //static uint8_t KeyOX62 = 0;
  static uint8_t Kbuf[6] = {0};

  //if(KeyOX62 == 0)
  //{
	  //KeyOX62++;
	  System *system;
	  system = get_system_conf();
	  Kbuf[0] = (uint8_t)(system->BlacklistMark & 0xff);
	  Kbuf[1] = (uint8_t)(system->Type & 0xff);
	  Kbuf[2] = (uint8_t)(system->Num & 0xff);
	  Kbuf[3] = (uint8_t)(system->BACK1 & 0xff);
	  Kbuf[4] = (uint8_t)(system->BACK2 & 0xff);
	  Kbuf[5] = (uint8_t)(system->BACK3 & 0xff);
  //}


  header->fun_code = 0x62;
  dev->proto_buf.len = sizeof(*header);


  uint16_t expected_num = dev->status_0x62.req_num;

  rwlock_rdlock();
  num = traverse_ed_status(&dev->status_0x62.last_0x62_edid, pack_0x62, dev);
  rwlock_unlock();
  logger_debug("Num of passed EDs:%d, tasks: %d, rest space: %d", num,
               expected_num - dev->status_0x62.req_num,
               dev->status_0x62.req_num);
  if (dev->status_0x62.req_num) dev->status_0x62.last_0x62_edid = 0;

  dev->proto_buf.data[dev->proto_buf.len++] = Kbuf[0];
  dev->proto_buf.data[dev->proto_buf.len++] = Kbuf[1];
  dev->proto_buf.data[dev->proto_buf.len++] = Kbuf[2];
  dev->proto_buf.data[dev->proto_buf.len++] = Kbuf[3];
  dev->proto_buf.data[dev->proto_buf.len++] = Kbuf[4];
  dev->proto_buf.data[dev->proto_buf.len++] = Kbuf[5];

  header->len = dev->proto_buf.len + sizeof(*crc);
  crc = (CRC_TYPE *)(dev->proto_buf.data + dev->proto_buf.len);
  *crc = MBCRC16(dev->proto_buf.data, dev->proto_buf.len);
  dev->proto_buf.len += sizeof(*crc);

  return Send2Serial(dev);
}

int send_0x6F(SerialDev *dev, uint32_t EDID) {
  // Not to be supported.
  UNUSED(dev);
  UNUSED(EDID);

  return 0;
}

int pack_0x62(void *opaque, void *data) {
  SerialDev *dev = opaque;
  EDStatus *status = data;
  Response0x62 *response = (Response0x62 *)(dev->proto_buf.data +
                                            dev->proto_buf.len);
  EDTask *task = NULL;
  struct Blob *blob = NULL;

  ASSERT(status != NULL);


  if (dev->status_0x62.wire_belong != (uint32_t)status->Belong) return 0;

  // logger_debug("EDID:%08X", status->EDID);
  dev->status_0x62.last_0x62_edid = status->EDID;

  task = get_ed_task(status->EDID);

  if (!task) {//任务删除
    response->EDID = status->EDID;
    response->pkt_num = 0;
    response->funcode = 0x59;
    // logger_debug("Funcode:%#x ,no task data.", response->funcode);
    response ++;
    dev->proto_buf.len += sizeof(*response);
    // 如果有图片任务，删除链表中数据
    listNode *node = listSearchKey(dev->ed_list, &status->EDID);
    if (node) {
      listDelNode(dev->ed_list, node);
      logger_debug("delete Pic list success.");
    }
    // 任务已满，退出遍历
    if (--dev->status_0x62.req_num == 0) return 1;
    // 已经删除，不需要后续判断
    return 0;
  }

  //////////////0x67////////////////
  if (task->PageMark && status->PageMark != task->PageMark)
    blob = &task->PagePic;
  else if (task->Page1Mark && status->Page1Mark != task->Page1Mark)
    blob = &task->Page1Pic;
  else if (task->Page2Mark && status->Page2Mark != task->Page2Mark)
    blob = &task->Page2Pic;
  else if (task->Page3Mark && status->Page3Mark != task->Page3Mark)
    blob = &task->Page3Pic;
  else if (task->Page4Mark && status->Page4Mark != task->Page4Mark)
    blob = &task->Page4Pic;
  else
    blob = NULL;

  if (blob) {
    uint8_t slice_num = (uint8_t)ceil((float)blob->nLen / PIC_SLICE_SIZE);
    response->EDID = status->EDID;
    response->pkt_num = slice_num;
    response->funcode = 0x67;
    logger_debug("EDID: %08X Funcode:%#x Check task true.", status->EDID, response->funcode);
    response ++;
    dev->proto_buf.len += sizeof(*response);

    logger_debug("PageMark[1-5] status:task [%02x:%02x %02x:%02x %02x:%02x %02x:%02x %02x:%02x]", \
        status->PageMark, task->PageMark, status->Page1Mark, task->Page1Mark, \
        status->Page2Mark, task->Page2Mark, status->Page3Mark, task->Page3Mark, \
        status->Page4Mark, task->Page4Mark);
    if (--dev->status_0x62.req_num == 0) return 1;
  }

  if (task->NFCMark && status->NFCMark != task->NFCMark) {
    response->EDID = status->EDID;
    response->pkt_num = 1;
    response->funcode = 0x63;
    logger_debug("EDID: %08X Funcode:%#x Check task true.", status->EDID, response->funcode);
    response ++;
    dev->proto_buf.len += sizeof(*response);
    if (--dev->status_0x62.req_num == 0) return 1;
  }

  if (task->LedMark && status->LEDMark != task->LedMark) {
    response->EDID = status->EDID;
    response->pkt_num = 1;
    response->funcode = 0x64;
    logger_debug("EDID: %08X Funcode:%#x Check task true.", status->EDID, response->funcode);
    response ++;
    dev->proto_buf.len += sizeof(*response);
    if (--dev->status_0x62.req_num == 0) return 1;
  }

  if (task->Black && !(task->Black == (uint8_t)status->Blacklist ||
    task->Black == (uint8_t)(status->Blacklist >> 8) ||
    task->Black == (uint8_t)(status->Blacklist >> 16) ||\
    task->Black == (uint8_t)(status->Blacklist >> 24))) {
    response->EDID = status->EDID;
    response->pkt_num = 1;
    response->funcode = 0x65;
    logger_debug("EDID: %08X Funcode:%#x Check task true.", status->EDID, response->funcode);
    response ++;
    dev->proto_buf.len += sizeof(*response);
    if (--dev->status_0x62.req_num == 0) return 1;
  }

  if (task->PageChange && status->CurPage != task->PageChange) {
    response->EDID = status->EDID;
    response->pkt_num = 1;
    response->funcode = 0x66;
    logger_debug("EDID: %08X Funcode:%#x Check task true.", status->EDID, response->funcode);
    response ++;
    dev->proto_buf.len += sizeof(*response);
    if (--dev->status_0x62.req_num == 0) return 1;
  }

  return 0;
}

int ed_match(void *pic, void *edid) {
  EDProperty *task = pic;
  unsigned int EDID = *(int *)edid;
  if (!task) return (1==0);
  return task->EDID == EDID;
}

void ed_free(void *ptr) {
  EDProperty *prop = ptr;
  if (prop->pic_task.data)
    free(prop->pic_task.data);
  free(prop);
}

struct EDProperty *ed_new(SerialDev *dev, uint32_t EDID) {
  EDProperty *prop = calloc(1, sizeof(*prop));
  MEM_CHECK_RPTR(prop);

  prop->EDID = EDID;
  listAddNodeTail(dev->ed_list, prop);
  logger_debug("calloc new EDProperty.");

  return prop;
}

int Send2Serial(SerialDev *dev) {
  logger_hexbuf("SERIAL TX", dev->proto_buf.data, dev->proto_buf.len);
  // gettimeofday(&end_time, NULL);
  // long usec = end_time.tv_usec - start_time.tv_usec;
  // long sec = end_time.tv_sec - start_time.tv_sec;
	// logger_info("total time: %ld msec",1000 * sec + usec/ 1000);
  return SerialSend(dev->fd.comfd, (const char *)dev->proto_buf.data,
                    dev->proto_buf.len);
}
