/*
 * @file: getmac.c
 * @create time: 2019-03-15 10:52:32
 * @last modified: 2019-03-15 10:52:32
 * @description:
 */
#include <stdio.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>        //for struct ifreq
#include <net/if_arp.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// 输入 www.baidu.com 类似格式
int socket_resolver(const char *domain, char* ipaddr)
{
    if (!domain || !ipaddr) return -1;

    char *http = "http";
    char *hn = (char*)domain;
    if (!strncmp(domain, http, strlen(http))) {
      hn = hn + strlen(http) + strlen("://");
      while (*hn == '/') hn ++;
    }

    struct hostent* host=gethostbyname(hn);
    if (!host) return -1;

    // 获取第一个IP地址
    strncpy(ipaddr, inet_ntoa(*(struct in_addr*)host->h_addr_list[0]), 16);

    return 0;
}


int is_valid_ip(const char *ip)
{
    if (ip == NULL || strlen(ip) < 7) {
        return -1;
    }

    int len = strlen(ip);
    const char *start = ip;
    const char *end = ip + len - 1;
    int ret = 0;
    int dot_count = 0;
    int cur_val = 0;
    int val = -1;

    while (end > start && isspace(*end)) {
        --end;
    }

    while (start < end && isspace(*start)) {
        ++start;
    }

    if (start >= end || isdigit(*start) == 0 || isdigit(*end) == 0) {
        return -1;
    }

    while (start <= end) {
        while (start <= end && '0' <= *start && *start <= '9') {
            cur_val = 10 * cur_val + (*start - '0');
            if (cur_val < 0 || cur_val > 255) {
                ret = -1;
                break;
            }
            val = cur_val;
            ++start;
        }
        if (start <= end) {
            if (*start == '.' && val != -1) {
                ++start;
                ++dot_count;
                cur_val = 0;
                val = -1;
            } else {
                ret = -1;
                break;
            }
        } else {
            break;
        }
    }

    if (ret == 0 && dot_count != 3) {
        ret = -1;
    }

    return ret;
}

int is_valid_mac(const char *mac)
{
    if (mac == NULL || strlen(mac) < 17) {
        return -1;
    }

    int len = strlen(mac);
    int ret = 0;
    int num_count = 0;
    int colon_count = 0;
    const char *start = mac;
    const char *end = mac + len - 1;

    while (end > start && isspace(*end)) {
        --end;
    }

    while (start < end && isspace(*start)) {
        ++start;
    }

    if ((end - start) != 16 || isxdigit(*start) == 0 || isxdigit(*end) == 0) {
        return -1;
    }

    while (start <= end) {
        while (start <= end && isxdigit(*start)) {
            ++start;
            if (++num_count > 2) {
                ret = -1;
                break;
            }
        }

        if (start <= end ) {
            if (*start == ':' && num_count == 2) {
                ++colon_count;
                num_count = 0;
                ++start;
            } else {
                ret = -1;
                break;
            }
        } else {
            break;
        }
    }

    if (ret == 0 && colon_count != 5) {
        ret = -1;
    }

    return ret;
}

int set_mac(unsigned char* mac, char *devname)
{
	struct ifreq ifreq;
	struct sockaddr* addr;
	int sock = 0;
	int ret = -1;

	if((0 != getuid()) && (0 != geteuid()))
		return -1;

	if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		return -1;
	}

	strcpy(ifreq.ifr_name, devname);
	addr = (struct sockaddr*)&ifreq.ifr_hwaddr;

	addr->sa_family = ARPHRD_ETHER;
	memcpy(addr->sa_data, mac, 6);

	ret = ioctl(sock, SIOCSIFHWADDR, &ifreq);

	close(sock);
	return ret;
}


int get_mac(unsigned char *mac, char *devname)    //返回值是实际写入char * mac的字符个数（不包括'\0'）
{
    struct ifreq ifreq;
    int sock;

    if ((sock = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror ("socket");
        return -1;
    }
    strcpy (ifreq.ifr_name, devname);    //Currently, only get eth0

    if (ioctl (sock, SIOCGIFHWADDR, &ifreq) < 0)
    {
        perror ("ioctl");
        return -1;
    }
    memcpy(mac, ifreq.ifr_hwaddr.sa_data, 6);
    return 0;
}


int get_ip(char *str, const char *devname)
{
    int sock_get_ip;
    char ipaddr[50];

    struct   sockaddr_in *sin;
    struct   ifreq ifr_ip;

    if (!str) return -1;

    strcpy(str, "0.0.0.0");
    if ((sock_get_ip=socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
         printf("socket create failse...GetLocalIp!/n");
         return -1;
    }

    memset(&ifr_ip, 0, sizeof(ifr_ip));
    strncpy(ifr_ip.ifr_name, devname, sizeof(ifr_ip.ifr_name) - 1);

    if( ioctl( sock_get_ip, SIOCGIFADDR, &ifr_ip) < 0 )
    {
         return -1;
    }
    sin = (struct sockaddr_in *)&ifr_ip.ifr_addr;
    strcpy(ipaddr,inet_ntoa(sin->sin_addr));

    printf("local ip:%s /n",ipaddr);
    close( sock_get_ip );

    strcpy(str, ipaddr);

    return 0;
}

int set_ip(const char *ipaddr, const char *devname)
{

    int sock_set_ip;

    struct sockaddr_in sin_set_ip;
    struct ifreq ifr_set_ip;

    bzero( &ifr_set_ip,sizeof(ifr_set_ip));

    if( ipaddr == NULL )
        return -1;

    sock_set_ip = socket( AF_INET, SOCK_STREAM, 0 );
    if(sock_set_ip == -1)
    {
        perror("socket create failse...SetLocalIp!/n");
        return -1;
    }

    memset( &sin_set_ip, 0, sizeof(sin_set_ip));
    strncpy(ifr_set_ip.ifr_name, devname, sizeof(ifr_set_ip.ifr_name)-1);

    sin_set_ip.sin_family = AF_INET;
    sin_set_ip.sin_addr.s_addr = inet_addr(ipaddr);
    memcpy( &ifr_set_ip.ifr_addr, &sin_set_ip, sizeof(sin_set_ip));

    if( ioctl( sock_set_ip, SIOCSIFADDR, &ifr_set_ip) < 0 )
    {
        perror( "Not setup interface/n");
        return -1;
    }

    //设置激活标志
    ifr_set_ip.ifr_flags |= IFF_UP |IFF_RUNNING;

    //get the status of the device
    if( ioctl( sock_set_ip, SIOCSIFFLAGS, &ifr_set_ip ) < 0 )
    {
         perror("SIOCSIFFLAGS");
         return -1;
    }

    close( sock_set_ip );
    return 0;
}


int get_netmask(char *str, const char *devname)
{
    int sock_netmask;
    char netmask_addr[50];

    struct ifreq ifr_mask;
    struct sockaddr_in *net_mask;

    if (!str) return -1;

    strcpy(str, "0.0.0.0");
    sock_netmask = socket( AF_INET, SOCK_STREAM, 0 );
    if( sock_netmask == -1)
    {
        perror("create socket failture...GetLocalNetMask/n");
        return -1;
    }

    memset(&ifr_mask, 0, sizeof(ifr_mask));
    strncpy(ifr_mask.ifr_name, devname, sizeof(ifr_mask.ifr_name )-1);

    if( (ioctl( sock_netmask, SIOCGIFNETMASK, &ifr_mask ) ) < 0 )
    {
        printf("mac ioctl error/n");
        return -1;
    }

    net_mask = ( struct sockaddr_in * )&( ifr_mask.ifr_netmask );
    strcpy( netmask_addr, inet_ntoa( net_mask -> sin_addr ) );

    printf("local netmask:%s/n",netmask_addr);

    close( sock_netmask );
    strcpy(str, netmask_addr);

    return 0;
}

int set_netmask(const char *szNetMask, const char *devname)
{
    int sock_netmask;

    struct ifreq ifr_mask;
    struct sockaddr_in *sin_net_mask;

    sock_netmask = socket( AF_INET, SOCK_STREAM, 0 );
    if( sock_netmask == -1)
    {
        perror("Not create network socket connect/n");
        return -1;
    }

    memset(&ifr_mask, 0, sizeof(ifr_mask));
    strncpy(ifr_mask.ifr_name, devname, sizeof(ifr_mask.ifr_name )-1);
    sin_net_mask = (struct sockaddr_in *)&ifr_mask.ifr_addr;
    sin_net_mask -> sin_family = AF_INET;
    inet_pton(AF_INET, szNetMask, &sin_net_mask ->sin_addr);

    if(ioctl(sock_netmask, SIOCSIFNETMASK, &ifr_mask ) < 0)
    {
        printf("sock_netmask ioctl error/n");
        return -1;
    }
    return 0;
}

int get_gateway(char *str)
{
    FILE *fp;
    char buf[512];
    char cmd[128];
    char gateway[30];
    char *tmp;

    if (!str) return -1;

    strcpy(str, "0.0.0.0");

    strcpy(cmd, "ip route");
    fp = popen(cmd, "r");
    if(NULL == fp)
    {
        perror("popen error");
        return -1;
    }
    while(fgets(buf, sizeof(buf), fp) != NULL)
    {
        tmp =buf;
        while(*tmp && isspace(*tmp))
            ++ tmp;
        if(strncmp(tmp, "default", strlen("default")) == 0)
            break;
    }
    sscanf(buf, "%*s%*s%s", gateway);
    printf("default gateway:%s/n", gateway);
    pclose(fp);

    strcpy(str, gateway);

    return 0;
}

int set_gateway(const char *szGateWay)
{
    int ret = 0;
    char cmd[128];
    char strGW[32];

    if(get_gateway(strGW) < 0) return -1;

    strcpy(cmd, "route del default gw ");
    strcat(cmd, strGW);
    ret = system(cmd);
    if(ret < 0)
    {
        perror("route error");
        return -1;
    }
    strcpy(cmd, "route add default gw ");
    strcat(cmd, szGateWay);

    ret = system(cmd);
    if(ret < 0)
    {
        perror("route error");
        return -1;
    }

    return ret;
}
#if 0
int main(int argc, char **argv)
{
  if (argc != 2) {
    printf("%s hostname\n",argv[0]);
    return 1;
  }
  char buf[100];
  int ret = socket_resolver(argv[1], buf);
  if (!ret)
    printf("ipaddr:%s\n",buf);
  else
    printf("ipaddr: error\n");
}
#endif
#if 0
int main()
{
    unsigned char szMac[18];
    char *devname = "eno1";
    int  nRtn = get_mac(szMac, devname);
    if(!nRtn)
    {
        fprintf(stderr, "MAC ADDR: %02x:%02x:%02x:%02x:%02x:%02x\n", szMac[0],szMac[1],szMac[2],szMac[3],szMac[4],szMac[5]);
    }
    char buf[100];

    get_ip(buf, devname);
    get_netmask(buf, devname);
    get_gateway(buf, devname);
    
    return 0;
}
#endif
