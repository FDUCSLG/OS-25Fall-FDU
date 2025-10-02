#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <driver/uart.h>
#include <kernel/core.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

static volatile bool boot_secondary_cpus = false;

void main() {
    if (cpuid() == 0) {
        /* @todo: Clear BSS section.*/

        smp_init();
        uart_init();
        printk_init();

        /* initialize kernel memory allocator */
        kinit();
        printk("hello, world! (Core 0)\n");

        
        // printk("edata is %p; end is %p\n", (void*)edata, (void*)end);

        /* @todo: Print "Hello, world! (Core 0)" */

        arch_fence();

        // Set a flag indicating that the secondary CPUs can start executing.
        boot_secondary_cpus = true;
    } else {
        while (!boot_secondary_cpus);
        arch_fence();
        printk("hello, world! (Core %lld)\n", cpuid());
        /* @todo: Print "Hello, world! (Core <core id>)" */
    }

    set_return_addr(idle_entry);
}
