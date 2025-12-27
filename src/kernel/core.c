#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <driver/virtio.h>
#include <common/string.h> 

volatile bool panic_flag;

NO_RETURN void idle_entry()
{
    //printk("[IDLE_ENTRY] idle_entry started on CPU%lld\n", cpuid());
    set_cpu_on();
    while (1) {
        yield();
        //printk("[IDLE_ENTRY] CPU%lld: back to idle\n", cpuid());
        if (panic_flag){
            //printk("[IDLE_ENTRY] CPU%lld: panic_flag is set\n", cpuid());
            break;
        }
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
    printk("Hello world! (Core %lld)\n", cpuid());
    proc_test();
    vm_test();
    user_proc_test();
    io_test();

    /* LAB 4 TODO 3 BEGIN
     * Read MBR (block 0) via virtio and parse partition table.
     * We record the second partition (index 1) start LBA and size.
     */
    static u8 mbr_buf[512];
    Buf tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.block_no = 0;
    tmp.flags = 0;
    if (virtio_blk_rw(&tmp) != 0) {
        printk("[ERROR] virtio_blk_rw failed for MBR\n");
        PANIC();
    }
    for (int i = 0; i < 512; i++) {
        mbr_buf[i] = tmp.data[i];
    }
    const MBR *mbr = (const MBR *)mbr_buf;
    if (mbr->signature != 0xAA55) {
        printk("[ERROR] Invalid MBR signature: 0x%04X\n", mbr->signature);
        PANIC();
    }
    static u32 part2_lba;
    part2_lba = mbr_partition2_lba(mbr);
    static u32 part2_sectors;
    part2_sectors = mbr_partition2_sectors(mbr);
    printk("[MBR] part2 start LBA=%u sectors=%u\n", part2_lba, part2_sectors);
    /* LAB 4 TODO 3 END */

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