#include <aarch64/intrinsic.h>
#include <driver/base.h>
#include <driver/interrupt.h>
#include <driver/irq.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <driver/gicv3.h>

static InterruptHandler int_handler[NUM_IRQ_TYPES];

static void default_handler(u32 intid)
{
    printk("\033[1;31m[Error CPU %lld]: Interrupt %d not implemented.\033[0m\n", cpuid(), intid);
    PANIC();
}

void init_interrupt()
{
    for (usize i = 0; i < NUM_IRQ_TYPES; i++) {
        int_handler[i] = default_handler;
    }
}

void set_interrupt_handler(InterruptType type, InterruptHandler handler)
{
    int_handler[type] = handler;
}

void interrupt_global_handler()
{
    //printk("[Interrupt] Interrupt occurred on CPU %lld.\n", cpuid());
    u32 iar = gic_iar();
    u32 intid = iar & 0x3ff;

    if (intid == 1023) {
        
        printk("\033[1;31m[Warning]: Spurious Interrupt.\033[0m\n");
        return;
    }

    gic_eoi(iar);

    if (int_handler[intid])
        int_handler[intid](intid);
}
