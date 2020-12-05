

#ifndef  __MAC__
#define __MAC__

// 输入 www.baidu.com 类似格式
int socket_resolver(const char *domain, char* ipaddr);
int is_valid_ip(const char *ip);
int is_valid_mac(const char *mac);
int set_mac(unsigned char* mac, char *devname);

int get_mac(unsigned char *mac, char *devname); //返回值是实际写入char * mac的字符个数（不包括'\0'）
int get_ip(char *str, const char *devname);
int set_ip(const char *ipaddr, const char *devname);
int get_netmask(char *str, const char *devname);
int set_netmask(const char *szNetMask, const char *devname);
int get_gateway(char *str);
int set_gateway(const char *szGateWay);

#endif
