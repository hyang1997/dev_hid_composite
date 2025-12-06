/*
 * lwIP configuration for HID Relay
 * Minimal config for UDP-only operation in poll mode
 */

#ifndef _LWIPOPTS_H_
#define _LWIPOPTS_H_

// Common settings for pico_cyw43_arch
#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

// Memory settings - minimal for UDP only
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_PBUF               16
#define PBUF_POOL_SIZE              16

// Enable UDP, disable TCP (we only need UDP)
#define LWIP_UDP                    1
#define LWIP_TCP                    0

// Enable DHCP to get an IP address
#define LWIP_DHCP                   1
#define LWIP_AUTOIP                 0
#define LWIP_DHCP_CHECK_LINK_UP     1

// ARP settings
#define LWIP_ARP                    1
#define ARP_QUEUEING                1

// ICMP for ping (useful for testing connectivity)
#define LWIP_ICMP                   1

// DNS (optional, but useful)
#define LWIP_DNS                    1

// Disable IPv6 to save space
#define LWIP_IPV6                   0

// Checksum settings - let hardware handle it where possible
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1

// Enable hostname (shows up on router)
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_RAW                    0
#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0

// Timeouts
#define LWIP_TIMERS                 1
#define LWIP_TIMEVAL_PRIVATE        0

#endif /* _LWIPOPTS_H_ */
