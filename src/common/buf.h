#pragma once

#include <common/defines.h>
#include <common/sem.h>

#define BSIZE 512
#define B_VALID 0x2 // Buffer has been read from disk.
#define B_DIRTY 0x4 // Buffer needs to be written to disk.

typedef struct {
    int flags;
    u8 data[BSIZE];
    u32 block_no;

    /* @todo: It depends on you to add other necessary elements. */
    Semaphore sem;
} Buf;
