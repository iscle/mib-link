#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS
#define LWIP_PLATFORM_DIAG(x) do { printf x; } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { fprintf(stderr,"lwIP: %s\n",x); abort(); } while(0)
#define LWIP_RAND() ((uint32_t)rand())
