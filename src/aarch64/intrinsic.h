#pragma once

#include <common/defines.h>

#define SECONDARY_CORE_ENTRY 0x40000000
#define PSCI_SYSTEM_OFF 0x84000008
#define PSCI_SYSTEM_RESET 0x84000009
#define PSCI_SYSTEM_CPUON 0xC4000003

/**
 * PSCI (Power State Coordination Interface) function on QEMU's virt platform
 * -------------------------------------------------------------------------
 * This function provides an interface to interact with the PSCI (Power State 
 * Coordination Interface) on ARM architectures, which is particularly useful 
 * in virtualized environments like QEMU's virt platform.
 *
 * Background:
 * PSCI is an ARM-defined interface that allows software running at the highest 
 * privilege level (typically a hypervisor or OS kernel) to manage power states 
 * of CPUs. It includes operations to turn CPUs on or off, put them into a low 
 * power state, or reset them.
 *
 * In a virtualized environment, such as when using QEMU with the virt machine 
 * type, the PSCI interface can be used to control the power states of virtual
 * CPUs (vCPUs). This is essential for operations like starting a secondary
 * vCPU or putting a vCPU into a suspend state.
 */
static ALWAYS_INLINE u64 psci_fn(u64 id, u64 arg1, u64 arg2, u64 arg3)
{
    u64 result;

    asm volatile("mov x0, %1\n"
                 "mov x1, %2\n"
                 "mov x2, %3\n"
                 "mov x3, %4\n"
                 "hvc #0\n"
                 "mov %0, x0\n"
                 : "=r"(result)
                 : "r"(id), "r"(arg1), "r"(arg2), "r"(arg3)
                 : "x0", "x1", "x2", "x3");

    return result;
}

static ALWAYS_INLINE u64 psci_cpu_on(u64 cpuid, u64 ep)
{
    return psci_fn(PSCI_SYSTEM_CPUON, cpuid, ep, 0);
}

static WARN_RESULT ALWAYS_INLINE usize cpuid()
{
    u64 id;
    asm volatile("mrs %[x], mpidr_el1" : [x] "=r"(id));
    return id & 0xff;
}

/* Instruct compiler not to reorder instructions around the fence. */
static ALWAYS_INLINE void compiler_fence()
{
    asm volatile("" ::: "memory");
}

static WARN_RESULT ALWAYS_INLINE u64 get_clock_frequency()
{
    u64 result;
    asm volatile("mrs %[freq], cntfrq_el0" : [freq] "=r"(result));
    return result;
}

static WARN_RESULT ALWAYS_INLINE u64 get_timestamp()
{
    u64 result;
    compiler_fence();
    asm volatile("mrs %[cnt], cntpct_el0" : [cnt] "=r"(result));
    compiler_fence();
    return result;
}

/* Instruction synchronization barrier. */
static ALWAYS_INLINE void arch_isb()
{
    asm volatile("isb" ::: "memory");
}

/* Data synchronization barrier. */
static ALWAYS_INLINE void arch_dsb_sy()
{
    asm volatile("dsb sy" ::: "memory");
}

static ALWAYS_INLINE void arch_fence()
{
    arch_dsb_sy();
    arch_isb();
}

/**
 * The `device_get/put_*` functions do not require protection using
 * architectural barriers. This is because they are specifically
 * designed to access device memory regions, which are already marked as
 * nGnRnE (Non-Gathering, Non-Reordering, on-Early Write Acknowledgement)
 * in the `kernel_pt_level0`.
 */
static ALWAYS_INLINE void device_put_u32(u64 addr, u32 value)
{
    compiler_fence();
    *(volatile u32 *)addr = value;
    compiler_fence();
}

static WARN_RESULT ALWAYS_INLINE u32 device_get_u32(u64 addr)
{
    compiler_fence();
    u32 value = *(volatile u32 *)addr;
    compiler_fence();
    return value;
}

/* Read Exception Syndrome Register (EL1). */
static WARN_RESULT ALWAYS_INLINE u64 arch_get_esr()
{
    u64 result;
    arch_fence();
    asm volatile("mrs %[x], esr_el1" : [x] "=r"(result));
    arch_fence();
    return result;
}

/* Reset Exception Syndrome Register (EL1) to zero. */
static ALWAYS_INLINE void arch_reset_esr()
{
    arch_fence();
    asm volatile("msr esr_el1, %[x]" : : [x] "r"(0ll));
    arch_fence();
}

/* Read Exception Link Register (EL1). */
static WARN_RESULT ALWAYS_INLINE u64 arch_get_elr()
{
    u64 result;
    arch_fence();
    asm volatile("mrs %[x], elr_el1" : [x] "=r"(result));
    arch_fence();
    return result;
}

/* Set vector base (virtual) address register (EL1). */
static ALWAYS_INLINE void arch_set_vbar(void *ptr)
{
    arch_fence();
    asm volatile("msr vbar_el1, %[x]" : : [x] "r"(ptr));
    arch_fence();
}

/* Flush TLB entries. */
static ALWAYS_INLINE void arch_tlbi_vmalle1is()
{
    arch_fence();
    asm volatile("tlbi vmalle1is");
    arch_fence();
}

/* Set Translation Table Base Register 0 (EL1). */
static ALWAYS_INLINE void arch_set_ttbr0(u64 addr)
{
    arch_fence();
    asm volatile("msr ttbr0_el1, %[x]" : : [x] "r"(addr));
    arch_tlbi_vmalle1is();
}

/* Get Translation Table Base Register 0 (EL1). */
static inline WARN_RESULT u64 arch_get_ttbr0()
{
    u64 result;
    arch_fence();
    asm volatile("mrs %[x], ttbr0_el1" : [x] "=r"(result));
    arch_fence();
    return result;
}

/* Set Translation Table Base Register 1 (EL1). */
static ALWAYS_INLINE void arch_set_ttbr1(u64 addr)
{
    arch_fence();
    asm volatile("msr ttbr1_el1, %[x]" : : [x] "r"(addr));
    arch_tlbi_vmalle1is();
}

/* Read Fault Address Register. */
static inline u64 arch_get_far()
{
    u64 result;
    arch_fence();
    asm volatile("mrs %[x], far_el1" : [x] "=r"(result));
    arch_fence();
    return result;
}

static inline WARN_RESULT u64 arch_get_tid()
{
    u64 tid;
    asm volatile("mrs %[x], tpidr_el1" : [x] "=r"(tid));
    return tid;
}

static inline void arch_set_tid(u64 tid)
{
    arch_fence();
    asm volatile("msr tpidr_el1, %[x]" : : [x] "r"(tid));
    arch_fence();
}

/* Get User Stack Pointer. */
static inline WARN_RESULT u64 arch_get_usp()
{
    u64 usp;
    arch_fence();
    asm volatile("mrs %[x], sp_el0" : [x] "=r"(usp));
    arch_fence();
    return usp;
}

/* Set User Stack Pointer. */
static inline void arch_set_usp(u64 usp)
{
    arch_fence();
    asm volatile("msr sp_el0, %[x]" : : [x] "r"(usp));
    arch_fence();
}

static inline WARN_RESULT u64 arch_get_tid0()
{
    u64 tid;
    asm volatile("mrs %[x], tpidr_el0" : [x] "=r"(tid));
    return tid;
}

static inline void arch_set_tid0(u64 tid)
{
    arch_fence();
    asm volatile("msr tpidr_el0, %[x]" : : [x] "r"(tid));
    arch_fence();
}

static ALWAYS_INLINE void arch_sev()
{
    asm volatile("sev" ::: "memory");
}

static ALWAYS_INLINE void arch_wfe()
{
    asm volatile("wfe" ::: "memory");
}

static ALWAYS_INLINE void arch_wfi()
{
    asm volatile("wfi" ::: "memory");
}

static ALWAYS_INLINE void arch_yield()
{
    asm volatile("yield" ::: "memory");
}

static ALWAYS_INLINE u64 get_cntv_ctl_el0()
{
    u64 c;
    asm volatile("mrs %0, cntv_ctl_el0" : "=r"(c));
    return c;
}

static ALWAYS_INLINE void set_cntv_ctl_el0(u64 c)
{
    asm volatile("msr cntv_ctl_el0, %0" : : "r"(c));
}

static ALWAYS_INLINE void set_cntv_tval_el0(u64 t)
{
    asm volatile("msr cntv_tval_el0, %0" : : "r"(t));
}

static inline WARN_RESULT bool _arch_enable_trap()
{
    u64 t;
    asm volatile("mrs %[x], daif" : [x] "=r"(t));
    if (t == 0)
        return true;
    asm volatile("msr daif, %[x]" ::[x] "r"(0ll));
    return false;
}

static inline WARN_RESULT bool _arch_disable_trap()
{
    u64 t;
    asm volatile("mrs %[x], daif" : [x] "=r"(t));
    if (t != 0)
        return false;
    asm volatile("msr daif, %[x]" ::[x] "r"(0xfll << 6));
    return true;
}

#define arch_with_trap                                          \
    for (int __t_e = _arch_enable_trap(), __t_i = 0; __t_i < 1; \
         __t_i++, __t_e || _arch_disable_trap())

static ALWAYS_INLINE NO_RETURN void arch_stop_cpu()
{
    while (1)
        arch_wfe();
}

#define set_return_addr(addr)                                       \
    (compiler_fence(),                                              \
     ((volatile u64 *)__builtin_frame_address(0))[1] = (u64)(addr), \
     compiler_fence())

void delay_us(u64 n);
u64 psci_cpu_on(u64 cpuid, u64 ep);
void smp_init();

static inline u64 arch_get_currentel()
{
    u64 result;
    asm volatile("mrs %[x], CurrentEL" : [x] "=r"(result));
    return result; // bits[3:2] indicate EL (e.g., 0x4 for EL1, 0x0 for EL0)
}

static inline u64 arch_get_sctlr()
{
    u64 result;
    asm volatile("mrs %[x], sctlr_el1" : [x] "=r"(result));
    return result;
}