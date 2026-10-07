// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MHI2_LWIPOPTS_H
#define MHI2_LWIPOPTS_H
#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_ICMP 1
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_DHCP 0
// DHCP servers must accept clients whose source address is still 0.0.0.0.
#define LWIP_IP_ACCEPT_UDP_PORT(port) ((port) == PP_NTOHS(67))
#define LWIP_DNS 0
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_SINGLE_NETIF 0
#define IP_FORWARD 0
#define MEM_ALIGNMENT 4
#define MEM_SIZE (32 * 1024)
#define MEMP_NUM_TCP_PCB 16
#define MEMP_NUM_TCP_PCB_LISTEN 6
#define MEMP_NUM_TCP_SEG 64
#define MEMP_NUM_UDP_PCB 4
#define PBUF_POOL_SIZE 24
#define PBUF_POOL_BUFSIZE 1600
#define TCP_MSS 1460
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN 32
#define TCP_QUEUE_OOSEQ 0
#define ETH_PAD_SIZE 0
#define LWIP_STATS 0
#define LWIP_CHKSUM_ALGORITHM 3
#define LWIP_RAND() ((uint32_t)rand())
#include <stdlib.h>
#endif
