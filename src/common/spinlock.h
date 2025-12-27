#pragma once

#include <common/defines.h>
#include <aarch64/intrinsic.h>

typedef struct {
    volatile bool locked;
} SpinLock;

void init_spinlock(SpinLock *);
WARN_RESULT bool try_acquire_spinlock(SpinLock *);
void acquire_spinlock(SpinLock *);
void release_spinlock(SpinLock *);
