#include <aarch64/trap.h>
#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <driver/interrupt.h>
#include <kernel/proc.h>
#include <kernel/syscall.h>

void trap_global_handler(UserContext *context)
{
    //printk("[TRAP] Trap occurred! ELR=0x%llx, ESR=0x%llx\n", context->elr, arch_get_esr());
    thisproc()->ucontext = context;

    u64 esr = arch_get_esr();
    u64 ec = esr >> ESR_EC_SHIFT;
    u64 iss = esr & ESR_ISS_MASK;
    u64 ir = esr & ESR_IR_MASK;

    (void)iss;

    arch_reset_esr();

    switch (ec) {
    case ESR_EC_UNKNOWN: {
        if (ir)
            PANIC();
        else
            interrupt_global_handler();
    } break;
    case ESR_EC_SVC64: {
        syscall_entry(context);
    } break;
    case ESR_EC_IABORT_EL0:
    case ESR_EC_IABORT_EL1:
    case ESR_EC_DABORT_EL0:
    case ESR_EC_DABORT_EL1: {
        printk("[ERROR] Page Fault Exception!\n");
        u64 far = arch_get_far();
        u64 elr = arch_get_elr();
        u64 ttbr0 = arch_get_ttbr0();
        int pid = thisproc() ? thisproc()->pid : -999;
        // Decode ISS basic fields (DFSC/IFSC low 6 bits, WnR bit for Data Abort)
        u64 fsc = iss & 0x3F; // Fault Status Code
        u64 is_write = (iss >> 6) & 1; // WnR for DAbort
        printk("[ERROR] pid=%d ec=0x%llx iss=0x%llx fsc=0x%llx wr=%llu FAR=0x%llx ELR=0x%llx TTBR0=0x%llx\n",
               pid, ec, iss, fsc, is_write, far, elr, ttbr0);
            // Extra diagnostics: current EL and SCTLR (to infer PAN/behavior)
            u64 cur_el = arch_get_currentel();
            u64 sctlr = arch_get_sctlr();
            printk("[ERROR] CurrentEL=0x%llx SCTLR_EL1=0x%llx\n", cur_el, sctlr);
        if (thisproc()) {
            extern void pt_dump_va(struct pgdir*, u64);
            pt_dump_va(&thisproc()->pgdir, far);
        }
        PANIC();
    } break;
    default: {
        printk("[ERROR] Unknwon exception %llu\n", ec);
        PANIC();
    }
    }

    // TODO: stop killed process while returning to user space
    if (thisproc()->killed == true && ((context->elr) & 0xffff000000000000) == 0) exit(-1);
}

NO_RETURN void trap_error_handler(u64 type)
{
    printk("[ERROR] Unknown trap type %llu\n", type);
    PANIC();
}
