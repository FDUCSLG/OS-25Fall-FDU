#pragma once

#include <kernel/proc.h>
#include <common/rbtree.h>

#define NCPU 4

struct sched {
    // TODO: customize your sched info
    Proc* thisproc;  // cpu当前正在运行的进程
    Proc* idle;  // 每个cpu都有一个idle进程（记录main函数的上下文自然演化而来）
};

struct cpu {
    bool online;
    struct rb_root_ timer;
    struct sched sched;
};

extern struct cpu cpus[NCPU];

void set_cpu_on();
void set_cpu_off();
