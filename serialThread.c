
#include "log.h"
#include "list.h"
#include "crc.h"
#include "main.h"
#include "sysinfo.h"
#include "gpioThread.h"
#ifdef USE_SERIAL
#include "serial.h"
#else
#define SerialSend(x,y,z) 0
#endif

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
//#include <linux/spi>


#define SPI_LONG_FRAME_SIZE   4096
#define SPI_SHORT_FRANE_SIZE  57
#define SPI_READ  0x80
#define SPI_WRITE 0x00
#define SPI_BASE        0
#define SPI_TASK_BEGIN  1
#define SPI_TASK_END    11

typedef struct SPI_REG_OPT {
  uint32_t addr:24;
  uint32_t opt:8;
} __PACKED SPI_REG_OPT;

//const SPI_REG_OPT SpiRegList[] = {
const SPI_REG_OPT SpiRegListTemp[] = {
  { .addr = 0xf01884, .opt = SPI_READ  }, // status
  { .addr = 0x301b84, .opt = SPI_WRITE }, // reg1
  { .addr = 0x302b84, .opt = SPI_WRITE }, // reg2
  { .addr = 0x303b84, .opt = SPI_WRITE },
  { .addr = 0x304b84, .opt = SPI_WRITE },
  { .addr = 0x305b84, .opt = SPI_WRITE },
  { .addr = 0x306b84, .opt = SPI_WRITE },
  { .addr = 0x307b84, .opt = SPI_WRITE },
  { .addr = 0x308b84, .opt = SPI_WRITE },
  { .addr = 0x309b84, .opt = SPI_WRITE },
  { .addr = 0x30ab84, .opt = SPI_WRITE },
  { .addr = 0x30bb84, .opt = SPI_WRITE }
};

/***************************************************************************/

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define CRC_TYPE uint16_t
#define TASK_NUM uint16_t
#define SPI_PAYLOAD (SPI_LONG_FRAME_SIZE - sizeof(SPI_REG_OPT) - sizeof(uint8_t) - sizeof(TASK_NUM) - sizeof(CRC_TYPE))

typedef struct SerialDev {
  const char *name;
  int fd;
  pthread_t tid;
  list *ed_list;
} SerialDev;

#define true  0
#define false 1

typedef struct StatusReg {
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
  uint8_t TaskTimeMark1;
  uint8_t TaskTimeMark2;
  uint8_t TaskTimeMark3;
  uint8_t TaskTimeMark4;
  uint16_t crc;
} __PACKED StatusReg;


typedef struct BigHead{
  uint8_t funcode;
  uint16_t len;
  uint32_t EDID;
  uint16_t  total;
  uint16_t cur;
  uint8_t  Day;   //(11/30)
  uint8_t  Hour;
  uint8_t  Min;
  uint8_t  Sec;
  uint8_t  SmallXor;
} __PACKED BigHead;

typedef struct BigTask{
  BigHead h;
  uint8_t data[SPI_PAYLOAD - sizeof(BigHead) - 1];
  uint8_t cs;
} __PACKED BigTask;

typedef struct BatchTask {
  uint32_t EDID;
  uint8_t  funcode;
  uint8_t  mark;
  uint16_t pkts;
  uint8_t  Success;
  uint8_t  Day;  //(11/30)
  uint8_t  Hour;
  uint8_t  Min;
  uint8_t  Sec;
} __PACKED BatchTask;

typedef struct sTime {
  uint8_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t min;
  uint8_t sec;
} __PACKED sTime;

typedef struct GivenEDID {
  uint32_t EDID;
  uint8_t block;          // uint8_t 类型，意味着图片不能超过1M大小
  uint8_t  mark;
} __PACKED GivenEDID;

typedef struct ResponseProbe {
  uint8_t key;
  uint8_t reg;
  uint8_t reserved;
  uint8_t task_f1[SPI_TASK_END - SPI_TASK_BEGIN + 1];
  uint8_t task_f2[SPI_TASK_END - SPI_TASK_BEGIN + 1];
  uint16_t big_num;
  uint16_t total;
  uint16_t white;
  sTime dt;
  uint8_t mark;
  uint8_t ap_type;
  uint8_t ap_count;
  uint8_t font_mark;
  uint8_t bak[3];
  GivenEDID which; // 指定要哪一个特定EDID，特定块时启用
  uint8_t page;
} __PACKED ResponseProbe;

typedef struct SpiProbeFrame {
  SPI_REG_OPT head;
  ResponseProbe rp;
  uint16_t crc;
} __PACKED SpiProbeFrame;


typedef struct SpiFrame {
  SPI_REG_OPT head;
  uint8_t  key;
  uint16_t num;  // 拆分图片帧时 = 0xffff，空时 = 0 ， 有多个任务时 > 0
  uint8_t data[SPI_PAYLOAD];
  uint16_t crc;
} __PACKED SpiFrame;

typedef struct TmpCache {
  uint32_t edid;// 拆分的图片时保持所属ID
  uint32_t idx; // 当前数据量
  SpiFrame frame;
} __PACKED TmpCache;

struct CacheList {
  TmpCache *cache;
  uint8_t size; // cache[size]
  uint8_t ap;   // 对应的天线编号
  uint32_t task_num;
  GivenEDID which; // 指定要哪一个特定EDID，特定块时启用
  list *ed_list;   // 图片任务分帧处理时使用
  uint32_t last_edid;
};

typedef struct EDProperty {
  uint32_t EDID;
  uint16_t block;
} EDProperty;

static void *ThreadSerial(void *arg);
static int spi_transfer(int fd, const uint8_t *tx, uint8_t *rx, size_t len);
static void fill_response_probe(ResponseProbe *probe);
static int query_white_list(void *opaque, void *data);
static int process_status(SpiFrame *, uint8_t *);
static int fill_task(struct CacheList *);
static int fill_white(struct CacheList *);
static int fill_task_mark(struct CacheList *);
static int fill_big_task(struct CacheList *, uint32_t, uint8_t, void *, size_t, size_t, size_t,uint8_t,uint8_t,uint8_t,uint8_t);
static int fill_to_cache(struct CacheList *, void *, size_t);
static int fill_batch_task(struct CacheList *, uint32_t, uint8_t, uint8_t, uint16_t,int,int,int,int,int);
static int get_tasks(void *, void *);
static int get_tasks2(void *, void *);
static uint8_t cs_sum(uint8_t *data, ssize_t size);

static int ed_match(void *, void *);
static void ed_free(void *);
static struct EDProperty *ed_new(list*, uint32_t);

static int task_num = 0;
static int task_exit = 0;
static SerialDev *serial_devs;


void print_buf(char *fmt, uint8_t *buf, int size)
{
	int i=0;
	printf("\n--------------------------------------------------------------------------------\n");
	printf("data:[%d]\n",size);
	for(; i<size; i++) printf(fmt,buf[i]);
	//printf("\n--------------------------------------------------------------------------------\n");
}

void printf_SPITX(char *fmt, uint8_t *buf, int size)
{
	int i=0;
	printf("TX:[%d]\n",size);
	for(; i<size; i++) printf(fmt,buf[i]);
	printf("\n");
}
void printf_SPIRX(char *fmt, uint8_t *buf, int size)
{
	int i=0;
	printf("RX:[%d]\n",size);
	for(; i<size; i++) printf(fmt,buf[i]);
	printf("\n");
}

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
  }

  return 0;
}

void stop_serial_tasks() {
  int idx;

  task_exit = 1;
  for (idx = 0; idx < task_num; idx++) {
    pthread_join(serial_devs[idx].tid, NULL);
    listRelease(serial_devs[idx].ed_list);
  }
  free(serial_devs);
}
static int check_crc(uint8_t *data, size_t len) {
  CRC_TYPE crc1 = MBCRC16(data, len);
  CRC_TYPE crc2 = *(uint16_t*)(data + len);

  if (crc1 != crc2) {
    logger_debug("Expected crc = %hx, but got %hx.", crc1, crc2);
    //print_buf("%02X ",data, len);
    return 0;
  }

  return 1;
}

static int spidev_init(const char* dev_name, uint32_t speed, uint8_t bits, uint32_t mode) {
  int fd = open(dev_name, O_RDWR);
  if (fd < 0) {
    logger_error("can't open device %s", dev_name);
    return -1;
  }
  int ret;
  if (mode) {// spi mode
	  //SPI_IOC_WR_MODE32
    ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
    if (ret == -1) {
      goto errout;
    }
  }
  ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
  if (ret == -1) {
    goto errout;
  }
  ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
  if (ret == -1) {
    goto errout;
  }

  return fd;

errout:
  logger_error("Parameter setting failed. %s", dev_name);
  close(fd);
  return -1;
}


static uint8_t cs_sum(uint8_t *data, ssize_t size) {
  uint8_t cs = 0;
  for (int i=0; i != size; i ++)
    cs ^= data[i];

  return cs;
}

// static int check_cs(SpiFrame frame) {
//   uint8_t cs = 0;
//   for (size_t i=0; i != sizeof(frame.data); i ++)
//     cs ^= frame.data[i];

//   return (cs == frame.cs);
// }

static int spi_transfer(int fd, const uint8_t *tx, uint8_t *rx, size_t len)
{
	struct spi_ioc_transfer tr;

	memset(&tr, 0, sizeof(tr));

	//logger_hexbuf("SPI TX", tx, len);

	tr.tx_buf = (unsigned long)(tx);
	tr.rx_buf = (unsigned long)(rx);
	tr.len = len;
	if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0) { // 射频芯片不回复时返回什么？
	return -1;
	}

	//logger_hexbuf("SPI RX", (const char*)rx, len);

  return 0;
}

void spidev_reset(const char *dev)
{
  const char *spidev[][2] = {
    {"/spidev0.0", U25_RESET},
    {"/spidev1.0", U21_RESET},
    {"/spidev2.0", U14_RESET},
    {"/spidev3.0", U2_RESET}
  };
  static uint16_t Errtimes[4] = {0};

  for (int i = 0; i < 4; i++) {
    if (!strcmp(spidev[i][0], dev))
    {
    	if(Errtimes[i] < 500) Errtimes[i]++;
    	//printf("  Current Err Occur at: %s   err time : %d  ",dev,Errtimes[i]);

    	if(Errtimes[i] >= 20){
    		printf("Reset SPI DEVICE: %s   \n",dev);

    		Errtimes[i] = 0;
            gpio_set_value(spidev[i][1], 0);
            sleep(1);
            gpio_set_value(spidev[i][1], 1);
    	}
    	//printf("--------------------------------------------------------------------------------\n");

      break;
    }
  }
}
// static void fill_probe_frame(SpiFrame *frame) {
#define fill_probe_frame(frame) do { \
  frame->head.addr = SpiRegList[SPI_BASE].addr; \
  frame->head.opt = SPI_READ; \
} while(0)

#define SPI_REC_SEQUENCE_NOCRC(SEND, RECV) \
    (RECV)->head.addr = 0x12345; \
    spi_transfer(dev->fd, (uint8_t*)(SEND), (uint8_t*)(RECV), sizeof(*(SEND))); \
    /*
    if ((RECV)->head.addr || (RECV)->head.opt) { \
      msleep(100); \
      continue; \
    }*/


#define SPI_REC_SEQUENCE(SEND, RECV) \
    (RECV)->head.addr = 0x12345; \
    spi_transfer(dev->fd, (uint8_t*)(SEND), (uint8_t*)(RECV), sizeof(*(SEND))); \
    if (!check_crc(((uint8_t*)(RECV)) + sizeof(SPI_REG_OPT), sizeof(*(RECV)) - sizeof(SPI_REG_OPT) - sizeof(CRC_TYPE) )) { \
	  spidev_reset(thread_name); \
      msleep(1000); \
      continue; \
    }

int ReadSPIRegs(const char *dev_name, SPI_REG_OPT *SpiRegList, size_t size) {
  char filename[64] = {0};
  //snprintf(filename, sizeof(filename), "./regs/%s", dev_name);
  snprintf(filename, sizeof(filename), "/root/regs/%s", dev_name);
  FILE *fp = fopen(filename, "r");
  if (!fp) return -1;

  char line[32];
  uint8_t i = 0;

  while(fgets(line, sizeof(line), fp) && i < size) {
    if (strlen(line) < 3 || strlen(line) > 12) continue;
    uint32_t v = 0;
    sscanf(line, "%x", &v);
    if (!v) continue;
    SpiRegList[i++].addr = v;
    logger_debug("SpiRegList[%d] = 0x%x", i - 1, v);
  }

  fclose(fp);

  return i;
}

void *ThreadSerial(void *arg) {
  SerialDev *dev = arg;
  char *thread_name = strrchr(dev->name, '/');
  SPI_REG_OPT SpiRegList[SPI_TASK_END];
  struct timeval tv;
  //uint64_t OldTime = 0,TmpTime = 0;
  static uint16_t Combo = 0;
  static uint32_t Cnttime = 0;
  static uint32_t Cnttime2 = 0;

  pthread_setspecific (tls_key_threadnr, thread_name + 1);
  logger_info("run...");

  do {
    dev->fd = spidev_init(dev->name, 2000000, 8, SPI_CPHA|SPI_CPOL);
    if (dev->fd < 0) {
      logger_error("%s spi open failed.", dev->name);
      sleep(3);
    }
  } while(!task_exit && dev->fd < 0);
  if (ReadSPIRegs(thread_name + 1, &SpiRegList, SPI_TASK_END) < SPI_TASK_END) {
    logger_error("Error reading configuration file!!!");
    return NULL;
  }

  SpiProbeFrame *send_frame = calloc(1, sizeof(SpiProbeFrame));
  SpiProbeFrame *recv_frame = calloc(1, SPI_LONG_FRAME_SIZE);
  SpiFrame *recv_task_frame = calloc(1, sizeof(SpiFrame));

  struct CacheList cachelist;
  cachelist.size = ARRAY_SIZE(SpiRegList) - 1;  // 排除探测帧
  cachelist.cache = (TmpCache *)calloc(cachelist.size, sizeof(TmpCache));
  cachelist.ed_list = dev->ed_list;
  cachelist.last_edid = 0;

  ASSERT(cachelist.cache != NULL);
  ASSERT(recv_task_frame != NULL);
  ASSERT(recv_frame != NULL);
  ASSERT(send_frame != NULL);

  listEmpty(dev->ed_list);

  while (!task_exit) {
    // gettimeofday(&start_time, NULL);
    fill_probe_frame(send_frame);
    memset(recv_frame, 0, sizeof(SpiProbeFrame));

    SPI_REC_SEQUENCE(send_frame, recv_frame)

    ResponseProbe *probe = &recv_frame->rp;
    ResponseProbe *resp_probe = &send_frame->rp;
    // ------- 判断价签状态是否准备好 -----------
    fill_response_probe(resp_probe);

    cachelist.task_num = 0;
    cachelist.which.EDID = 0;
    for (int i = 0; i < cachelist.size; i ++) {
      cachelist.cache[i].idx = 0;
      cachelist.cache[i].edid = 0;
      cachelist.cache[i].frame.num = 0;
    }


    if ((probe->key == 1 && probe->reg < SPI_TASK_END) ||
        (probe->key == 7 && probe->reg == 0)) {
      // key = 1 说明价签状态准备好了
      // 读取寄存器块中的价签数据

		gettimeofday(&tv, NULL);
		logger_info("STEP <--------------------Current loop : %d---------------------------->",Cnttime++);
		logger_info("STEP 1 :start"); // 秒
		logger_info("STEP second: %ld", tv.tv_sec); // 秒
		logger_info("STEP millisecond: %ld", tv.tv_sec * 1000 + tv.tv_usec / 1000); // 毫秒
		//logger_info("STEP <----------------------------------------------------------------->");


      for (int i = 0; i < probe->reg; i ++) {
        cachelist.cache[i].frame.head = SpiRegList[SPI_TASK_BEGIN + i];
        cachelist.cache[i].frame.head.opt = SPI_READ;

        // SPI_REC_SEQUENCE_NOCRC((SpiFrame *)&cachelist.cache[i].frame, recv_task_frame)
        spi_transfer(dev->fd, (uint8_t*)(&cachelist.cache[i].frame), \
                              (uint8_t*)(recv_task_frame), \
                              sizeof(cachelist.cache[i].frame));

        process_status(recv_task_frame, &cachelist.ap);
        //usleep(500000);
      }
      //if(probe->key == 1){
      	//sleep(1);
      	//usleep(100000);
      //}

      	/*
		gettimeofday(&tv, NULL);
		logger_info("STEP <----------------------------------------------------------------->");
		logger_info("STEP 2 :fill_task_mark(&cachelist)  before"); // 秒
		logger_info("STEP second: %ld", tv.tv_sec); // 秒
		logger_info("STEP millisecond: %ld", tv.tv_sec * 1000 + tv.tv_usec / 1000); // 毫秒
		logger_info("STEP <----------------------------------------------------------------->");
		*/

		// key = 7 说明射频芯片支持linux自由发送
		resp_probe->total = fill_task_mark(&cachelist);
		resp_probe->key = 0x02; // 批量任务

		/*
		gettimeofday(&tv, NULL);
		logger_info("STEP <----------------------------------------------------------------->");
		logger_info("STEP 3 :fill_task_mark(&cachelist)  after "); // 秒
		logger_info("STEP second: %ld", tv.tv_sec); // 秒
		logger_info("STEP millisecond: %ld", tv.tv_sec * 1000 + tv.tv_usec / 1000); // 毫秒
		logger_info("STEP <----------------------------------------------------------------->");
		*/

    }
	else if (probe->key == 3) { // key = 3 说明射频芯片请求任务，edid > 0 单个价签任务
      // 射频请求某个edid的任务

	  	gettimeofday(&tv, NULL);
		logger_info("STEP <-----------------------PicChannel %d----------------------------->",Cnttime2++);
		logger_info("STEP 3"); // 秒
		logger_info("STEP second: %ld", tv.tv_sec); // 秒
		logger_info("STEP millisecond: %ld", tv.tv_sec * 1000 + tv.tv_usec / 1000); // 毫秒
		//logger_info("STEP <----------------------------------------------------------------->");

      cachelist.which = probe->which;
      resp_probe->total = fill_task(&cachelist);
      resp_probe->key = 0x04;
    } else if (probe->key == 5) { // key = 5 说明射频芯片请求白名单任务
      resp_probe->total = fill_white(&cachelist);
      resp_probe->key = 0x06;
    } else {
      // 运行到此处说明 linux 发送的数据 射频芯片还未读取完
      usleep(100000);
      //sleep(1);
      //logger_info("run...3");
      continue;
    }


    int i = 0;


    for (; i < cachelist.size && i < SPI_TASK_END; i ++) {
      if (!cachelist.cache[i].idx) break;
      cachelist.cache[i].frame.head = SpiRegList[SPI_TASK_BEGIN + i];
      cachelist.cache[i].frame.key = resp_probe->key;
      // SPI Frame: Addr + R/W + DATA + CRC
      // CRC = MBCRC16( DATA )
      //cachelist.cache[i].frame.crc = MBCRC16((uint8_t*)&cachelist.cache[i].frame.num, offsetof(SpiFrame, crc) - offsetof(SpiFrame, num));
        cachelist.cache[i].frame.crc = MBCRC16((uint8_t*)&cachelist.cache[i].frame.key, offsetof(SpiFrame, crc) - offsetof(SpiFrame, key));
        spi_transfer(dev->fd, (uint8_t*)&cachelist.cache[i].frame, (uint8_t*)recv_frame, SPI_LONG_FRAME_SIZE);

    }

    /*
	gettimeofday(&tv, NULL);
	logger_info("STEP <----------------------------------------------------------------->");
	logger_info("STEP 4"); // 秒
	logger_info("STEP second: %ld", tv.tv_sec); // 秒
	logger_info("STEP millisecond: %ld", tv.tv_sec * 1000 + tv.tv_usec / 1000); // 毫秒
	logger_info("STEP <----------------------------------------------------------------->");
	*/

    resp_probe->reg = i; // 有效数据占用
    send_frame->head = SpiRegList[SPI_BASE];
    send_frame->head.opt = SPI_WRITE;
    send_frame->crc = MBCRC16((uint8_t*)&send_frame->rp, sizeof(ResponseProbe));
    spi_transfer(dev->fd, (uint8_t*)send_frame, (uint8_t*)recv_frame, sizeof(*send_frame));

	gettimeofday(&tv, NULL);
	logger_info("STEP <----------------------------------------------------------------->");
	logger_info("STEP 5"); // 秒
	logger_info("STEP second: %ld", tv.tv_sec); // 秒
	logger_info("STEP millisecond: %ld", tv.tv_sec * 1000 + tv.tv_usec / 1000); // 毫秒
	//logger_info("STEP <----------------------------------------------------------------->");

	usleep(100000);
    //sleep(1);
    //
    // --- 发送完spi最后一帧后，执行任务情况统计  否则  sleep
    //
    //logger_info("run...4");
  }

  //logger_info("run...5");

  free(cachelist.cache);
  free(recv_task_frame);
  free(recv_frame);
  free(send_frame);
  logger_info("exit...");
  if (dev->fd > 0) close(dev->fd);

  return NULL;
}

static void fill_response_probe(ResponseProbe *probe) {
  probe->key = 0x08;
  probe->reg = 0x0;
  probe->reserved = 0;

  SysTime tm;
  get_sys_time(&tm);
  probe->dt.year = tm.year;
  probe->dt.month = tm.month;
  probe->dt.day = tm.day;
  probe->dt.hour = tm.hour;
  probe->dt.min = tm.minute;
  probe->dt.sec = tm.second;

  rwlock_rdlock();

  System *system = get_system_conf();
  probe->mark = system->Mark;
  probe->ap_type = system->Type;
  probe->ap_count = system->Num;
  probe->font_mark = 0x00;
  probe->bak[0] = system->BACK1;
  probe->bak[1] = system->BACK2;
  probe->bak[2] = system->BACK3;
  probe->white = system->WhiteNum;

  rwlock_unlock();

}

static int fill_white(struct CacheList *cache) {
  rwlock_rdlock();
  traverse_white_list(&cache->last_edid, query_white_list, cache);
  rwlock_unlock();

  logger_info("------ fill white list [%d]-----------", cache->task_num);
  return cache->task_num;
}


static int query_white_list(void *opaque, void *data) {
  struct CacheList *cache = (struct CacheList*)opaque;
  WhiteList *wl = data;

  cache->last_edid = wl->EDID;

  if (wl->Flag) {
    return fill_to_cache(cache, &wl->EDID, 4);
  }

  return 0;
}
// // 如何实现优先图片任务或者优先大帧
// static int PriorityFill()
// {
//   int max = 4000;
//   // 1. 先遍历一遍图片任务，将图片大于max 的图片填入cache
//   // 2. 如果cache没有被填满，则 遍历大于 max/2 的任务 
// }
//
static int fill_task_mark(struct CacheList *cachelist) {

  rwlock_rdlock();
  traverse_ed_task(&cachelist->last_edid, get_tasks, cachelist);
  rwlock_unlock();

  return cachelist->task_num;
}

static int fill_task(struct CacheList *cache) {
  rwlock_rdlock();
  if (cache->which.EDID) { // 单查
    EDTask *task = get_ed_task(cache->which.EDID);
    if (task) get_tasks2(cache, task);
  } else {
    traverse_ed_task(&cache->last_edid, get_tasks2, cache);
  }
  rwlock_unlock();

  return cache->task_num;
}


static int process_status(SpiFrame *frame, uint8_t *ap) {
  EDStatus *status;
  time_t t = time(NULL);
  struct tm res;
  int taskstatus[5] = {0,0,0,0,0};

  for (int i=0; i != frame->num; i ++) {
    StatusReg *req = ((StatusReg *)frame->data) + i;
    rwlock_rdlock();
    taskstatus[0] = taskstatus[1] = taskstatus[2] = taskstatus[3] = taskstatus[4]  = 0;
    EDTask *task = get_ed_task(req->EDID);
    if(task)
    {
    	taskstatus[0] = task->PageMark;
    	taskstatus[1] = task->Page1Mark;
    	taskstatus[2] = task->Page2Mark;
    	taskstatus[3] = task->Page3Mark;
    	taskstatus[4] = task->Page4Mark;
    }
    rwlock_unlock();
    logger_info("EDID:%#x %u  PageMark-Task: %d-%d , %d-%d , %d-%d , %d-%d , %d-%d  ED-TaskTimeMark:%d-%d-%d-%d", req->EDID,req->EDID, \
    		req->page_mark,taskstatus[0],\
			req->page1_mark,taskstatus[1],\
			req->page2_mark,taskstatus[2],\
			req->page3_mark,taskstatus[3],\
			req->page4_mark,taskstatus[4],\
			req->TaskTimeMark1,req->TaskTimeMark2,req->TaskTimeMark3,req->TaskTimeMark4);

    ASSERT(req->EDID != 0);
    if (!check_crc((uint8_t*)req, sizeof(StatusReg) - MEM_SIZE(StatusReg, crc))) {
      continue;
    }
    rwlock_wrlock();
    status = get_ed_status(req->EDID);
    if (!status) {
      logger_info("New edid %08x found on serial\n", req->EDID);
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
    // status->Blacklist = req->store_id;
    status->Belong = req->wire_belog;
    *ap = req->wire_belog;

    //if (!status->StartTime)
      //status->StartTime = calloc(1, 20);
    status->StartTime = realloc(status->StartTime, 20);
    MEM_CHECK_R_1(status->StartTime);
    snprintf(status->StartTime, 20, "%d-%d-%d %d:%d", req->start_year,
            req->start_month, req->start_day, req->start_hour,
            req->start_min);
    //if (!status->EndTime)
    //status->EndTime = calloc(1, 20);
    status->EndTime = realloc(status->EndTime, 20);
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

    status->TaskTimeMark1 = req->TaskTimeMark1;
    status->TaskTimeMark2 = req->TaskTimeMark2;
    status->TaskTimeMark3 = req->TaskTimeMark3;
    status->TaskTimeMark4 = req->TaskTimeMark4;

    //if (!status->LastTime)
    //status->LastTime = calloc(1, 20);
    status->LastTime = realloc(status->LastTime, 20);

    localtime_r(&t, &res);
    if(t > status->time)
    {
      MEM_CHECK_R_1(status->LastTime);
      snprintf(status->LastTime, 20, "%d-%d-%d %d:%d", res.tm_year - 100,
          res.tm_mon + 1, res.tm_mday, res.tm_hour,
          res.tm_min);
      status->time = t;
    }
    rwlock_unlock();
  }

  logger_info("############# Number of EDID Status: %d ############# ",(uint16_t)(frame->num));

  return 0;
}


// 查询 cachelist 中有多少空闲buf
static int get_idle_cache(struct CacheList *cache) {
  uint8_t idle = 0;
  for (int i = 0; i != cache->size; i ++) {
    if (!cache->cache[i].idx) {
      idle ++;
    }
  }

  return idle;
}


// 将任务数据填充到合适的位置
// 返回0 填充成功  返回1 填充失败
static int fill_to_cache(struct CacheList *cache, void *data, size_t size) {
  for (int i = 0; i != cache->size; i ++) {
    // 最后两个crc字节
    if (SPI_PAYLOAD - cache->cache[i].idx >= size) {
      // 只有在 bigtask 时才正确
      cache->cache[i].edid = *(uint32_t*)(((uint8_t*)data) + 3);
      memcpy(cache->cache[i].frame.data + cache->cache[i].idx, data, size);
      cache->cache[i].idx += size;
      cache->cache[i].frame.num ++;
      cache->task_num ++;
      return 0;
    }
  }

  return 1;
}

// 将任务数据填充到合适的位置
// 返回0 填充成功  返回1 填充失败
static int fill_batch_task(struct CacheList *cache, uint32_t edid, uint8_t funcode, uint8_t mark, uint16_t pkts,int success,int hour,int min,int sec,int Day) {
  BatchTask bt = {
    .EDID = edid,
    .funcode = funcode,
    .mark = mark,
    .pkts = pkts,
	.Success = success,
	.Hour = hour,
	.Min  = min,
	.Sec  = sec,
	.Day  = Day
  };

  return fill_to_cache(cache, &bt, sizeof(bt));
}

static int get_tasks(void *opaque, void *data) {
  struct CacheList *cache = opaque;
  EDTask *task = data;
  ASSERT(task != NULL);
  EDStatus *status = get_ed_status(task->EDID);
  EDStatus tmp_status;
  uint8_t over = 0;

  if (!status) {
    status = &tmp_status;
    memset(status, 0 , sizeof(EDStatus));
  }

  //logger_info("------ fill batch task [%08X]-----------", task->EDID);
  cache->last_edid = task->EDID;

  //printf("Current Antenna NO: 0X%02X \n",(uint8_t)(cache->ap));

  // 只处理本天线下价签的任务
//  if (status->Belong != cache->ap)
//  {
//	  logger_info("status->Belong Wrong!!!!!!!!");
//	  return 0;
//  }

  if (task->PageChange && status->CurPage != task->PageChange) {
    over += fill_batch_task(cache, task->EDID, 0x73, task->PageChange, 1,task->SuccessFlag,task->Hour,task->Min,task->Sec,task->Day);
  }
  if (task->LedMark && status->LEDMark != task->LedMark) {
    over += fill_batch_task(cache, task->EDID, 0x71, task->LedMark, 1,task->SuccessFlag,task->Hour,task->Min,task->Sec,task->Day);
  }
  if (task->NFCMark && status->NFCMark != task->NFCMark) {
    uint16_t pkt_size = MEM_SIZE(BigTask, data);;
    uint8_t slice_num = (uint8_t)ceil((float)strlen(task->NFCData) / pkt_size);
    over += fill_batch_task(cache, task->EDID, 0x72, task->NFCMark, slice_num,task->SuccessFlag,task->Hour,task->Min,task->Sec,task->Day);
  }

  uint8_t PicIdx = -1;
  uint8_t PicMask = -1;
  uint8_t X = 0;
  struct Blob *blob = NULL;

  if((status->TaskTimeMark1 != task->Day)  || \
     (status->TaskTimeMark2 != task->Hour) || \
	 (status->TaskTimeMark3 != task->Min)  || \
	 (status->TaskTimeMark4 != task->Sec)){
	   X = 1;
  }


  if (task->PageMark && (X > 0)) {
    blob = &task->PagePic;
    PicIdx = 1;
    PicMask = task->PageMark;
    //printf("PageMark = %d X = %d \n",task->PageMark,X);

  } else if (task->Page1Mark && (X > 0)) {
    blob = &task->Page1Pic;
    PicIdx = 2;
    PicMask = task->Page1Mark;
    //printf("PageMark = %d X = %d \n",task->PageMark,X);

  } else if (task->Page2Mark && (X > 0)) {
    blob = &task->Page2Pic;
    PicIdx = 3;
    PicMask = task->Page2Mark;
    //printf("PageMark = %d X = %d \n",task->PageMark,X);

  } else if (task->Page3Mark && (X > 0)) {
    blob = &task->Page3Pic;
    PicIdx = 4;
    PicMask = task->Page3Mark;
    //printf("PageMark = %d X = %d \n",task->PageMark,X);

  } else if (task->Page4Mark && (X > 0)) {
    blob = &task->Page4Pic;
    PicIdx = 5;
    PicMask = task->Page4Mark;
    //printf("PageMark = %d X = %d \n",task->PageMark,X);
  } else {
    blob = NULL;
    //printf("PageTask none\n");
  }
  logger_debug("FILL Batch Task EDID [%08X %08u] ~~~~~ status:task [%d:%d -- %d:%d -- %d:%d -- %d:%d]", task->EDID, task->EDID, \
    status->TaskTimeMark1, task->Day, status->TaskTimeMark2, task->Hour, \
    status->TaskTimeMark3, task->Min, status->TaskTimeMark4, task->Sec);

  if (blob) {
    uint16_t pkt_size = MEM_SIZE(BigTask, data);;
    uint8_t slice_num = (uint8_t)ceil((float)blob->nLen / pkt_size);
    over += fill_batch_task(cache, task->EDID, PicIdx, PicMask, slice_num,task->SuccessFlag,task->Hour,task->Min,task->Sec,task->Day);
  }

  return over;
}


// 将任务数据填充到合适的位置
// 返回0 填充成功  返回1 填充失败或大帧没有完全填充完
static int fill_big_task( struct CacheList *cache,
                          uint32_t edid,
                          uint8_t funcode,
                          void *data,
                          size_t size,
                          size_t total,
                          size_t cur,
						  uint8_t hour,
						  uint8_t min,
						  uint8_t sec,
						  uint8_t Day) {
  BigTask bt = {
    .h.funcode = funcode,
    .h.EDID = edid,
    .h.total = total,
	.h.Hour = hour,
	.h.Min = min,
	.h.Sec = sec,
	.h.Day = Day
  };

  ASSERT(total > 0);
  ASSERT(cur > 0);
  ASSERT(size > 0);
  ASSERT(data != NULL);

  if (size <= MEM_SIZE(BigTask, data)) {
    memcpy(bt.data, data, size);
    bt.h.len = size;
    bt.h.cur = cur;
    bt.data[size] = cs_sum((uint8_t*)&bt.h, size + sizeof(BigHead));
    bt.h.SmallXor = cs_sum((uint8_t*)&bt.h, sizeof(BigHead)-1);


    return fill_to_cache(cache, &bt, sizeof(BigHead) + size + 1);
  }

  uint16_t pkt_size = MEM_SIZE(BigTask, data);
  uint16_t slice_num = total - cur; // 此处 实际剩余  total - cur + 1个
  int i = 0;

  for (; i != slice_num; i ++) {                        // 剩余  total - cur
    memcpy(bt.data, data + pkt_size * i, pkt_size);
    bt.h.len = pkt_size;
    bt.h.cur = cur + i;
    bt.cs = cs_sum((uint8_t*)&bt.h, bt.h.len + sizeof(BigHead));
    bt.h.SmallXor = cs_sum((uint8_t*)&bt.h, sizeof(BigHead)-1);

    if (fill_to_cache(cache, &bt, sizeof(bt)))
      goto out;
  }

  size_t remain = size - pkt_size * i;                  // 剩余  + 1
  memcpy(bt.data, data + pkt_size * i, remain);
  bt.h.len = remain;
  bt.h.cur = cur + i;
  bt.data[remain] = cs_sum((uint8_t*)&bt.h, bt.h.len + sizeof(BigHead));
  bt.h.SmallXor = cs_sum((uint8_t*)&bt.h, sizeof(BigHead)-1);

  if (fill_to_cache(cache, &bt, sizeof(BigHead) + remain + 1) == 0) {
    return 0;
  }

out:
  return 1;
}

static int get_tasks2(void *opaque, void *data) {
  struct CacheList *cache = opaque;
  EDTask *task = data;
  ASSERT(task != NULL);
  EDStatus *status = get_ed_status(task->EDID);
  EDStatus tmp_status;
  uint8_t over = 0;

  if (!status) {
    status = &tmp_status;
    memset(status, 0 , sizeof(EDStatus));
  }

  logger_info("------ fill big task [%08X]-----------", task->EDID);

  cache->last_edid = task->EDID;

  // 只处理本天线下价签的任务
  //if (status->Belong != cache->ap) return 0;

  if (task->PageChange && status->CurPage != task->PageChange) {
    over += fill_big_task(cache, task->EDID, 0x73, &task->PageChange, 1, 1, 1,task->Hour,task->Min,task->Sec,task->Day);
  }
  if (task->LedMark && status->LEDMark != task->LedMark) {
    uint8_t tmpbuf[] = {
                          task->LedMark,
                          task->Led1Mark,
                          task->Led2Mark,
                          task->Led3Mark,
                          task->Led4Mark,
                        };

    over += fill_big_task(cache, task->EDID, 0x71, tmpbuf, sizeof(tmpbuf), 1, 1,task->Hour,task->Min,task->Sec,task->Day);
  }
  if (task->NFCMark && status->NFCMark != task->NFCMark && task->NFCData) {
    uint16_t pkt_size = MEM_SIZE(BigTask, data);;
    uint8_t slice_num = (uint8_t)ceil((float)strlen(task->NFCData) / pkt_size);
    over += fill_big_task(cache, task->EDID, 0x72, task->NFCData, strlen(task->NFCData), slice_num, 1,task->Hour,task->Min,task->Sec,task->Day);
  }

  uint8_t X = 0;
  uint8_t PicIdx = -1;
  struct Blob *blob = NULL;
  listNode *node = listSearchKey(cache->ed_list, &task->EDID);
  EDProperty *prop = NULL;
  if (node) prop = node->value;

  if((status->TaskTimeMark1 != task->Day)  || \
     (status->TaskTimeMark2 != task->Hour) || \
	 (status->TaskTimeMark3 != task->Min)  || \
	 (status->TaskTimeMark4 != task->Sec)){
	   X = 1;
  }


  if (task->PageMark && (X > 0)) {
    blob = &task->PagePic;
    PicIdx = 1;
  } else if (task->Page1Mark && (X > 0)) {
    blob = &task->Page1Pic;
    PicIdx = 2;
  } else if (task->Page2Mark && (X > 0)) {
    blob = &task->Page2Pic;
    PicIdx = 3;
  } else if (task->Page3Mark && (X > 0)) {
    blob = &task->Page3Pic;
    PicIdx = 4;
  } else if (task->Page4Mark && (X > 0)) {
    blob = &task->Page4Pic;
    PicIdx = 5;
  } else {
    /*****直接删除？需要释放内存么？******/
    if (node) listDelNode(cache->ed_list, node);
    blob = NULL;
  }

  if (blob) {
    if (!cache->which.block && prop) {
      cache->which.block = prop->block;
      logger_info("--------------------------last block=%d-----------------------", prop->block);
    }

    uint16_t pkt_size = MEM_SIZE(BigTask, data);
    uint8_t slice_num = (uint8_t)ceil((float)blob->nLen / pkt_size);

    if (cache->which.block > slice_num) {
      cache->which.block = 0; //错误的block忽略掉
      logger_warning("EDID: %08X request %d block, but only %d block!", cache->which.EDID, \
          cache->which.block, slice_num);
    }

    uint8_t idle = get_idle_cache(cache);

    if (cache->which.block) cache->which.block --; // 块编号从 1 开始
    int need = slice_num - cache->which.block;

    if (!idle && need < 1) return 1;  // 最后一块可以能只有几个字节，可能能够填入拼组帧

    over += fill_big_task(cache, task->EDID, PicIdx, \
        blob->data + cache->which.block * pkt_size, \
        blob->nLen - pkt_size * cache->which.block, \
        slice_num,									\
        (cache->which.block + 1),task->Hour,task->Min,task->Sec,task->Day);

    if (over != 0 && !node) { // 填充了一部分
      // over = 0 说明数据全部填充成功, != 0 说明填充了一部分
      // idle > 0 说明至少填充了一部分
      prop = ed_new(cache->ed_list, task->EDID);
    }
    if (prop) {
      prop->block = cache->which.block + idle + 1;
      if (prop->block >= slice_num) // 发送完成后，重新从头开始
        prop->block = 1;
    }
  }

  return over;
}

int ed_match(void *pic, void *edid) {
  EDProperty *task = pic;
  unsigned int EDID = *(int *)edid;
  if (!task) return (1==0);
  return task->EDID == EDID;
}

void ed_free(void *ptr) {
  EDProperty *prop = ptr;
  free(prop);
}

struct EDProperty *ed_new(list *l, uint32_t EDID) {
  EDProperty *prop = calloc(1, sizeof(*prop));
  MEM_CHECK_RPTR(prop);

  prop->EDID = EDID;
  prop->block = 0;
  listAddNodeTail(l, prop);
  logger_debug("calloc new EDProperty.");

  return prop;
}

