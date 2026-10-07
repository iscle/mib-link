#pragma once
#include "../../platform/pico_w/lwipopts.h"
#undef MEM_ALIGNMENT
#define MEM_ALIGNMENT 8
#define SYS_LIGHTWEIGHT_PROT 0
