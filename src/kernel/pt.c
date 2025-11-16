#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>
#include <kernel/printk.h>
#include <kernel/cpu.h>

// Needed for fallback TTBR0 when no L0 is present
extern PTEntries invalid_pt;

PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc)
{
    // TODO:
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.
    // Return Kernel Address.
    PTEntriesPtr pt0 = pgdir->pt, pt1 = NULL, pt2 = NULL, pt3 = NULL;
    if(pt0 == NULL) {
        if(alloc) {
            pgdir->pt = kalloc_page();
            memset(pgdir->pt,0,PAGE_SIZE);
        }
        else return NULL;
    }
    pt0 = pgdir->pt;
    pt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt0[VA_PART0(va)]));
    if(!(pt0[VA_PART0(va)] & PTE_VALID)){
        if(alloc){
            pt1 = kalloc_page();
            memset(pt1,0,PAGE_SIZE);
            pt0[VA_PART0(va)] = K2P(pt1) | PTE_TABLE;
        }
        else return NULL;
    }
    pt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt1[VA_PART1(va)]));
    if(!(pt1[VA_PART1(va)] & PTE_VALID)){
        if(alloc){
            pt2 = kalloc_page();
            memset(pt2,0,PAGE_SIZE);
            pt1[VA_PART1(va)] = K2P(pt2) | PTE_TABLE;
        }
        else return NULL;
    }
    pt3 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt2[VA_PART2(va)]));
    if(!(pt2[VA_PART2(va)] & PTE_VALID)){
        if(alloc){
            pt3 = kalloc_page();
            memset(pt3,0,PAGE_SIZE);
            pt2[VA_PART2(va)] = K2P(pt3) | PTE_TABLE;
        }
        else return NULL;
    }
    return &(pt3[VA_PART3(va)]);
}

void init_pgdir(struct pgdir *pgdir)
{
    pgdir->pt = NULL;
}

void free_pgdir(struct pgdir *pgdir)
{
    // TODO:
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    PTEntriesPtr pt0 = pgdir->pt;
    if(pt0 == NULL) return;
    for(int i=0;i<N_PTE_PER_TABLE;++i){
        if(pt0[i] & PTE_VALID){
            PTEntriesPtr pt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt0[i]));
            for(int j=0;j<N_PTE_PER_TABLE;++j){
                if(pt1[j] & PTE_VALID){
                    PTEntriesPtr pt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt1[j]));
                    for(int k=0;k<N_PTE_PER_TABLE;++k){
                        if(pt2[k] & PTE_VALID){
                            PTEntriesPtr pt3 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt2[k]));
                            kfree_page((void*)pt3);
                        }
                    }
                    kfree_page((void*)pt2);
                }
            }
            kfree_page((void*)pt1);
        }
    }
    kfree_page((void*)pgdir->pt);
    pgdir->pt = NULL;
}

void attach_pgdir(struct pgdir *pgdir)
{
    //{
        //void *l0 = pgdir->pt ? (void *)pgdir->pt : (void *)&invalid_pt;
        //printk("[PT] attach_pgdir cpu=%lld ttbr0 <= %llx (pt=%p)\n", cpuid(), K2P(l0), pgdir->pt);
    //}
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

void pt_dump_va(struct pgdir *pgdir, u64 va)
{
    printk("[ERROR][PT] dump va=0x%llx\n", va);
    if (!pgdir || !pgdir->pt) {
        printk("[ERROR][PT] pgdir or L0 is NULL\n");
        return;
    }

    PTEntriesPtr pt0 = pgdir->pt;
    int i0 = VA_PART0(va);
    PTEntry pte0 = pt0[i0];
    printk("[ERROR][PT]  L0[%d]=0x%llx (addr=0x%llx, flags=0x%llx)\n", i0, pte0, PTE_ADDRESS(pte0), PTE_FLAGS(pte0));
    if (!(pte0 & PTE_VALID)) { printk("  L0 invalid\n"); return; }

    PTEntriesPtr pt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pte0));
    int i1 = VA_PART1(va);
    PTEntry pte1 = pt1[i1];
    printk("[ERROR][PT]  L1[%d]=0x%llx (addr=0x%llx, flags=0x%llx)\n", i1, pte1, PTE_ADDRESS(pte1), PTE_FLAGS(pte1));
    if (!(pte1 & PTE_VALID)) { printk("[ERROR][PT]  L1 invalid\n"); return; }
    if ((pte1 & 0x3) == PTE_BLOCK) { printk("[ERROR][PT]  L1 is BLOCK\n"); return; }

    PTEntriesPtr pt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pte1));
    int i2 = VA_PART2(va);
    PTEntry pte2 = pt2[i2];
    printk("[ERROR][PT]  L2[%d]=0x%llx (addr=0x%llx, flags=0x%llx)\n", i2, pte2, PTE_ADDRESS(pte2), PTE_FLAGS(pte2));
    if (!(pte2 & PTE_VALID)) { printk("[ERROR][PT]  L2 invalid\n"); return; }
    if ((pte2 & 0x3) == PTE_BLOCK) { printk("[ERROR][PT]  L2 is BLOCK\n"); return; }

    PTEntriesPtr pt3 = (PTEntriesPtr)P2K(PTE_ADDRESS(pte2));
    int i3 = VA_PART3(va);
    PTEntry pte3 = pt3[i3];
    printk("[ERROR][PT]  L3[%d]=0x%llx (addr=0x%llx, flags=0x%llx)\n", i3, pte3, PTE_ADDRESS(pte3), PTE_FLAGS(pte3));
    if (!(pte3 & PTE_VALID)) { printk("[ERROR][PT]  L3 invalid\n"); return; }
    printk("[ERROR][PT]  L3 is PAGE\n");
}
