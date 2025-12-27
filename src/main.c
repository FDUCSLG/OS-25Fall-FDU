#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <driver/uart.h>
#include <kernel/core.h>
#include <kernel/cpu.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <driver/interrupt.h>
#include <kernel/proc.h>
#include <driver/gicv3.h>
#include <driver/timer.h>
#include <driver/virtio.h>

static volatile bool boot_secondary_cpus = false;

void main()
{
    if (cpuid() == 0) {
        printk("========= Booting ==========\n");
        extern char edata[], end[];
        memset(edata, 0, (usize)(end - edata));
        printk("[KERNEL] BSS section cleared: %p - %p\n", edata, end);
        /* initialize interrupt handler */
        init_interrupt();
        /* initialize uart and printk */
        uart_init();
        printk_init();
        /* initialize GICv3 */
        gicv3_init();
        gicv3_init_percpu();
        init_clock_handler();

        /* Initialize kernel memory allocator. */
        kinit();

        /* Initialize sched. */
        init_sched();

        /* Initialize kernel proc. */
        init_kproc();

        virtio_init();

        smp_init();
        //printk("CPU 0 are booted.\n");
        arch_fence();
        // Set a flag indicating that the secondary CPUs can start executing.
        boot_secondary_cpus = true;
        printk("========== Booted ==========\n");
    } else {
        while (!boot_secondary_cpus)
            ;
        arch_fence();
        gicv3_init_percpu();
    }
    printk("[KER] CPU%lld: entering idle_entry, idle_entry is %p\n", cpuid(), idle_entry);  
    set_return_addr(idle_entry);
}
