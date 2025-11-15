#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>

volatile bool panic_flag;

NO_RETURN void idle_entry()
{
    printk("[IDLE_ENTRY] idle_entry started on CPU%lld\n", cpuid());
    set_cpu_on();
    while (1) {
        yield();
        //printk("[IDLE_ENTRY] CPU%lld: back to idle\n", cpuid());
        if (panic_flag)
            break;
        arch_with_trap
        {
            //printk("[IDLE_ENTRY] CPU%lld: going to WFI\n", cpuid());
            arch_wfi();
            //printk("[IDLE_ENTRY] CPU%lld: WFI returned\n", cpuid());
        }
    }
    set_cpu_off();
    arch_stop_cpu();
}

NO_RETURN void kernel_entry()
{
    printk("[KERNEL] kernel_entry started on CPU%lld\n", cpuid());
    proc_test();

    while (1)
        yield();
}

NO_INLINE NO_RETURN void _panic(const char *file, int line)
{
    printk("\033[1;31m=====%s:%d PANIC%lld!=====\033[0m\n", file, line, cpuid());
    panic_flag = true;
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) {
        if (cpus[i].online)
            i--;
    }
    printk("\033[1;31mKernel PANIC invoked at %s:%d. Stopped.\033[0m\n", file, line);
    arch_stop_cpu();
}