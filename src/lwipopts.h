#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H
// Los navegadores abren varias conexiones a la vez
#define MEMP_NUM_TCP_PCB 16
#define MEMP_NUM_TCP_SEG 64
#define MEM_SIZE         16000

#include "lwipopts_examples_common.h"
#endif
