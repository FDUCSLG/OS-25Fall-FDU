#define PAGE_SIZE 4096

#define SCTLR_MMU_ENABLED (1 << 0)

/* Translation Control Register */
#define TCR_T0SZ        (64 - 48)
#define TCR_T1SZ        ((64 - 48) << 16)
#define TCR_TG0_4K      (0 << 14)
#define TCR_TG1_4K      (2 << 30)
#define TCR_SH0_INNER   (3 << 12)
#define TCR_SH1_INNER   (3 << 28)
#define TCR_SH0_OUTER   (2 << 12)
#define TCR_SH1_OUTER   (2 << 28)
#define TCR_ORGN0_IRGN0 ((1 << 10) | (1 << 8))
#define TCR_ORGN1_IRGN1 ((1 << 26) | (1 << 24))
#define TCR_VALUE       (TCR_T0SZ | TCR_T1SZ | TCR_TG0_4K | TCR_TG1_4K | TCR_SH0_OUTER | TCR_SH1_OUTER | TCR_ORGN0_IRGN0 | TCR_ORGN1_IRGN1)

/* Memory region attributes */
#define MT_DEVICE_nGnRnE       0x0
#define MT_NORMAL              0x1
#define MT_NORMAL_NC           0x2
#define MT_DEVICE_nGnRnE_FLAGS 0x00
#define MT_NORMAL_FLAGS        0xFF  /* Inner/Outer Write-back Non-transient RW-Allocate */
#define MT_NORMAL_NC_FLAGS     0x44  /* Inner/Outer Non-cacheable */
#define MAIR_VALUE             ((MT_DEVICE_nGnRnE_FLAGS << (8 * MT_DEVICE_nGnRnE)) | (MT_NORMAL_FLAGS << (8 * MT_NORMAL)) | (MT_NORMAL_NC_FLAGS << (8 * MT_NORMAL_NC)))

.section ".text.boot"

.global _start
_start:
  /**
   * Set up the user and kernel page tables.
   * Higher and lower half map to same physical memory region.
   */
  adrp x9, kernel_pt_level0
  msr ttbr0_el1, x9
  msr ttbr1_el1, x9

  ldr x9, =TCR_VALUE
  msr tcr_el1, x9

  ldr x9, =MAIR_VALUE
  msr mair_el1, x9

  /* Enable MMU. */
  mrs x9, sctlr_el1
  orr x9, x9, #SCTLR_MMU_ENABLED
  msr sctlr_el1, x9

  /* Set up kernel stacks. */
  mrs x0, mpidr_el1
  and x0, x0, #0xff
  add x0, x0, 1
  mov x1, #PAGE_SIZE 
  mul x0, x0, x1
  ldr x2, =kstack
  add x2, x2, x0
  mov sp, x2
  ldr x9, =main
  br  x9

.section ".data"
.align 12
.global kstack
kstack:
  .zero 4096 
  .zero 4096 
  .zero 4096 
  .zero 4096
  /**
   * Allocate a guard page to protect the kernel stack from potential overflow
   * or corruption. 
   */
  .zero 4096
