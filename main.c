/*-----------------------------------------------------------------------------------------------
2019-08-05  WireBelong 0x42 Function change
2019-12-06  0x6d add respond second bit
-----------------------------------------------------------------------------------------------*/
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include "main.h"
#include "mac.h"
#include "log.h"
#include "sysinfo.h"
#include "netThread.h"
#include "debugThread.h"
#include "serialThread.h"
#include "gpioThread.h"

Handle_t db_handle = INVALID_HANDLE;
int sys_id = 1;
int restart = 0;
atomic_t task_update = ATOMIC_INIT(1);
pthread_key_t tls_key_threadnr;
pthread_rwlock_t grwlock = PTHREAD_RWLOCK_INITIALIZER;

void rwlock_rdlock(void) {
  pthread_rwlock_rdlock(&grwlock);
}

void rwlock_wrlock(void) {
  pthread_rwlock_wrlock(&grwlock);
}

void rwlock_unlock(void) {
  pthread_rwlock_unlock(&grwlock);
}


static void prepare(const char *dbname) {
  db_handle = db_open(dbname);
  if (db_handle == INVALID_HANDLE) {
    logger_error("Fail to open DB %s.\n", dbname);
    exit(0);
  }

  db_create_table(db_handle, "EDTask");
  db_create_table(db_handle, "EDStatus");
  db_create_table(db_handle, "System");
  db_create_table(db_handle, "WireStatus");

  /* Check if system conf exists. If not, restore the default. */
  System *SystemConf = get_system_conf();
  if (!SystemConf) {
    SystemConf = db_alloc_row("System");
    SystemConf->ID = sys_id;
  }

  if (!SystemConf->Version) {
    SystemConf->Version = 0x3434;
    SystemConf->BACK1 = 0x29;
    SystemConf->BACK2 = 0x01;
    SystemConf->BACK3 = 0;

    SystemConf->MAC = realloc(SystemConf->MAC, 18);
    unsigned char mac[6];
    get_mac(mac, NET_DEV_NAME);
    snprintf(SystemConf->MAC, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    SystemConf->IPAddr = realloc(SystemConf->IPAddr, 16);
    get_ip(SystemConf->IPAddr, NET_DEV_NAME);

    SystemConf->NetMask = realloc(SystemConf->NetMask, 16);
    get_netmask(SystemConf->NetMask, NET_DEV_NAME);

    SystemConf->GateWay = realloc(SystemConf->GateWay, 16);
    get_gateway(SystemConf->GateWay);
    db_cached_insert_row(db_handle, SystemConf);
    db_update_row(db_handle, SystemConf);
  }

  for (int idx = 0; idx < MAX_SERIAL_NUM; idx++) {
    int wire_belong = idx + 1;
    WireStatus *wstatus = get_wire_status(wire_belong);
    if (!wstatus) {
      wstatus = db_alloc_row("WireStatus");
      wstatus->WireID = wire_belong;
      db_cached_insert_row(db_handle, wstatus);
      db_update_row(db_handle, wstatus);
    }
  }
}

void sig_handler(int sig) {
  logger_info("get signal %d.\n", sig);
  restart = 1;
}

// just for test
// int test_task(void *opaque, void *data) {
//   EDTask *task = data;
//   logger_debug("EDID:%ld",task->EDID);
//   return 0;
// }

int main(int argc, const char **argv) {
  time_t last_update = time(NULL);
//  const char *dev[] = { "/dev/ttyACM0",
//                        "/dev/ttyACM1",
//                        "/dev/ttyACM2",
//                        "/dev/ttyACM3"};
  if (argc < 2) {
    printf("Usage: %s dbfile\n", argv[0]);
    exit(0);
  }

#if 0
  printf("goto daemon...\n");
  daemon(0,0);
#endif

  openlog("baozi", LOG_PID|LOG_CONS, LOG_USER);

  pthread_key_create(&tls_key_threadnr, NULL);
  pthread_setspecific (tls_key_threadnr, "Main");
  logger_info("system run...");
  print_sysinfo();
  prepare(argv[1]);

  // traverse_ed_task(NULL, test_task, NULL);

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);
  signal(SIGUSR1, sig_handler);

  //start_serial_tasks(sizeof(dev)/sizeof(dev[0]), dev);

  start_SPI10_task();

  start_debug_task();
  start_gpio_task();
  start_net_task();


  while (!restart) {
    time_t now = time(NULL);
    if (now - last_update > 1800) {
      if (atomic_dec_and_test(&task_update)) {
        rwlock_rdlock();
        atomic_set(&task_update, 6); // 有任务更新时  task_update = 1
        db_cached_sync(db_handle, "EDTask");
        db_cached_sync(db_handle, "WireStatus");
        //db_cached_sync(db_handle, "EDStatus");
        rwlock_unlock();
      }
      last_update = now;
    }
    SLEEP(10, 0);
  }
  //stop_serial_tasks();
  db_cached_sync(db_handle, "EDTask");
  db_cached_sync(db_handle, "WireStatus");
  //db_cached_sync(db_handle, "EDStatus");
  print_sysinfo();
  db_close(&db_handle);
  logger_info("system exit...");
  closelog();

  return 0;
}
