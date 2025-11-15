#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/proc.h>
#include <aarch64/mmu.h>

struct cpu cpus[NCPU];

void set_cpu_on()
{
    ASSERT(!_arch_disable_trap());
    // disable the lower-half address to prevent stupid errors
    extern PTEntries invalid_pt;
    arch_set_ttbr0(K2P(&invalid_pt));
    extern char exception_vector[];
    arch_set_vbar(exception_vector);
    //printk("[CPU]CPU %lld: VBAR set to %p\n", cpuid(), exception_vector);
    arch_reset_esr();
    cpus[cpuid()].online = true;
    printk("[CPU] CPU %lld: hello\n", cpuid());
}

void set_cpu_off()
{
    _arch_disable_trap();
    cpus[cpuid()].online = false;
    printk("[CPU] CPU %lld: stopped\n", cpuid());
}
