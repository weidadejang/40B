
#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

typedef unsigned char u8_t;
typedef unsigned short u16_t;
typedef unsigned int u32_t;
typedef void *OBJECT;

#define QUERY_SYS_CODE        0x70
#define TASK_LED_CODE         0x71
#define TASK_NFC_CODE         0x72
#define TASK_CHANGEPAGE_CODE  0x73
#define TASK_PIC_CODE         0x74
#define QUERY_TASK_CODE       0x75
#define DELETE_TASK_CODE      0x76
#define SET_WHITE_LIST_CODE   0x77
#define QUERY_STATUS_CODE     0x78
#define DELETE_STATUS_CODE    0x79
#define SYNC_TIME_CODE        0x7a
#define SET_SYS_CODE          0x7b




#define MAX_NUM_OF_JQ 800
// 一个价签60多个字节  一个任务6个字节
#define MAX_NUM_OF_TASK 800 * 10
#define NFC_STR_MAX_LEN 360
#define URL_STR_MAX_LEN 360
//#define MAX_TAB_A_SIZE NFC_STR_MAX_LEN + 64
#define MAX_TAB_A_SIZE 67
#define MAX_TCP_SEND_PACKET (32 + MAX_NUM_OF_JQ * MAX_TAB_A_SIZE)

#define MAX(v1, v2) (v1) > (v2) ? (v1) : (v2)
#define MIN(v1, v2) (v1) < (v2) ? (v1) : (v2)

#define CRC_TYPE uint16_t

typedef struct TcpHeader {
  uint8_t fun_code;
  uint16_t len;
} __attribute__((packed, aligned(1))) TcpHeader;

typedef struct TcpHeader2 {
  uint8_t fun_code;
  uint32_t len;
} __attribute__((packed, aligned(1))) TcpHeader2;

typedef struct Cmd0x51 {
  uint8_t table;
  uint16_t start_addr;
  uint16_t end_addr;
  uint8_t data[0];
} __attribute__((packed, aligned(1))) Cmd0x51;

typedef struct Response0x51 {
  uint8_t status;
} __attribute__((packed, aligned(1))) Response0x51;


typedef struct Cmd0x52 {
  uint16_t num;
  uint8_t black;
  uint32_t EDID[0];
} __attribute__((packed, aligned(1))) Cmd0x52;

typedef struct Response0x52 {
  uint16_t num;
} __attribute__((packed, aligned(1))) Response0x52;


typedef struct Cmd0x53 {
  uint8_t table;
  uint16_t start_addr;
  uint16_t end_addr;
} __attribute__((packed, aligned(1))) Cmd0x53;

#define HEAD_TYPE uint16_t
#define END_TYPE uint16_t
typedef struct Response0x53 {
  uint8_t table;
  uint16_t num;
  uint8_t  data[0];
} __attribute__((packed, aligned(1))) Response0x53;


typedef struct LEDData {
  uint32_t EDID;
  uint8_t  LedMark;
  uint8_t  Led1Mark;
  uint8_t  Led2Mark;
  uint8_t  Led3Mark;
  uint8_t  Led4Mark;
} __attribute__((packed, aligned(1))) LEDData;

typedef struct Cmd0x54 {
  uint16_t num;
  LEDData  LED[0];
} __attribute__((packed, aligned(1))) Cmd0x54;

typedef struct Response0x5X {
  uint16_t num;
  uint32_t EDID[0];
} __attribute__((packed, aligned(1))) Response0x5X;


typedef struct NFCData {
  uint32_t EDID;
  uint16_t Len;
  uint8_t  NFCMark;
  uint8_t  Data[0];
} __attribute__((packed, aligned(1))) NFCData;

typedef struct Cmd0x55 {
  uint16_t num;
  NFCData  NFC[0];
} __attribute__((packed, aligned(1))) Cmd0x55;

typedef struct PageData {
  uint32_t EDID;
  uint8_t  PageMark;
} __attribute__((packed, aligned(1))) PageData;

typedef struct Cmd0x56 {
  uint16_t num;
  PageData page[0];
} __attribute__((packed, aligned(1))) Cmd0x56;

typedef struct uint24_t {
  uint8_t c[3];
} __attribute__((packed, aligned(1))) uint24_t;
typedef struct PictureData {
  uint16_t Head;
  uint24_t Len;
  uint8_t  Page;
  uint8_t  PageFooter;
  uint32_t EDID;
  uint8_t  goods_mark;
  uint8_t  PageMark;
  uint8_t  Data[0];
} __attribute__((packed, aligned(1))) PictureData;

typedef struct Cmd0x57 {
  uint16_t num;
  PictureData JQ[0];
} __attribute__((packed, aligned(1))) Cmd0x57;

typedef struct Cmd0x59 {
  uint16_t num;
  uint32_t EDID[0];
} __attribute__((packed, aligned(1))) Cmd0x59;

typedef struct Cmd0x5A {
  uint16_t num;
  uint32_t EDID[0];
} __attribute__((packed, aligned(1))) Cmd0x5A;

typedef struct PacketA {
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
  uint32_t black_list;
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
  uint8_t data_type;
  uint8_t last_year;
  uint8_t last_month;
  uint8_t last_day;
  uint8_t last_hour;
  uint8_t last_min;
  uint8_t reserved[4];
  uint8_t tpage_change;
  uint8_t tgoods_mark;
  uint8_t tpage_mark;
  uint8_t tpage1_mark;
  uint8_t tpage2_mark;
  uint8_t tpage3_mark;
  uint8_t tpage4_mark;
  uint8_t tpage_footer;
  uint8_t tled_mark;
  uint8_t tled1_mark;
  uint8_t tled2_mark;
  uint8_t tled3_mark;
  uint8_t tled4_mark;
  uint8_t tnfc_mark;
} __attribute__((packed, aligned(1))) PacketA;

typedef struct Response0x5A {
  uint16_t num;
  PacketA  pack[0];
} __attribute__((packed, aligned(1))) Response0x5A;

typedef struct Cmd0x5c {
  uint16_t num;
  uint32_t EDID[0];
} __attribute__((packed, aligned(1))) Cmd0x5c;

typedef struct ResponseHead {
  uint16_t head;
  uint8_t fun_code;
  uint16_t len;
  uint8_t  data[0];
} __attribute__((packed, aligned(1))) ResponseHead;

typedef struct Cmd0x7A {
  uint8_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minutes;
  uint8_t seconds;
} __attribute__((packed, aligned(1))) Cmd0x7A;


#define Response0x7A Cmd0x7A



typedef struct DownPacket {
  u32_t EDID;
  size_t size;
  u8_t *data;
} __attribute__((packed, aligned(1))) DownPacket;

typedef struct UpPacket {
  u16_t size;
  u16_t len;
  u8_t data[0];
} __attribute__((packed, aligned(1))) UpPacket;

typedef struct TcpContext {
  u32_t last_edid;
  UpPacket *up_packet;
}TcpContext;

typedef struct WriePacket {
  u8_t fun_code;
  u8_t belong;
  u32_t EDID;
  u32_t other;
} __attribute__((packed, aligned(1))) WriePacket;

typedef struct OnePacket {
  uint16_t head;
  uint8_t fun_code;
  union {
    uint16_t sLen;
    uint32_t lLen;
  } L;
} __attribute__((packed, aligned(1))) OnePacket;



#endif
