#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H
// Los navegadores abren varias conexiones a la vez
#define MEMP_NUM_TCP_PCB 16
#define MEMP_NUM_TCP_SEG 64
#define MEM_SIZE         16000

#include "lwipopts_examples_common.h"

// HTTPS (actualizaciones desde GitHub)
#define LWIP_ALTCP             1
#define LWIP_ALTCP_TLS         1
#define LWIP_ALTCP_TLS_MBEDTLS 1
// Un registro TLS ocupa hasta ~16 KB y no se descifra hasta tenerlo entero:
// la ventana TCP tiene que ser mayor o se queda atascado
#undef TCP_WND
#define TCP_WND 32768

// Multicast para que Alexa encuentre la vitrina (SSDP)
#define LWIP_IGMP 1

// cumplenico.local en la WiFi externa (mDNS)
#define LWIP_MDNS_RESPONDER            1
#define LWIP_NUM_NETIF_CLIENT_DATA     1
#define MDNS_RESP_USENETIF_EXTCALLBACK 1
#define MEMP_NUM_UDP_PCB               8
// mDNS usa varios temporizadores; sin margen, lwIP se queda sin y se cuelga
#define MEMP_NUM_SYS_TIMEOUT           (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 8)
#endif
