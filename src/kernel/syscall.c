#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <test/test.h>
#include <aarch64/intrinsic.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"

void *syscall_table[NR_SYSCALL] = {
    [0 ... NR_SYSCALL - 1] = NULL,
    [SYS_myreport] = (void *)syscall_myreport,
};

void syscall_entry(UserContext *context)
{
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    u64 id = context->x[8];
    // printk("syscall_table function id: %d\n", (int)id);
    u64 x[6];
    for(int i = 0; i < 6; i++) { x[i] = context->x[i]; }
    void* sys_func = syscall_table[id];
    // be sure to check the range of id. if id >= NR_SYSCALL, panic.
    if (id >= NR_SYSCALL) PANIC();
    // if (sys_func == NULL) while(true) continue;
    // if (sys_func == NULL) PANIC();
    else context->x[0] = ((u64(*)(u64,u64,u64,u64,u64,u64))
        (sys_func))(x[0],x[1],x[2],x[3],x[4],x[5]);
}

#pragma GCC diagnostic pop