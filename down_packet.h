
#ifndef __DOWN_PACKET_H__
#define __DOWN_PACKET_H__

void logout(void);
int init_global_buf(size_t size);
void destory_temp_buf(void);
char *analysis_json_packet(const char *);

#endif 

