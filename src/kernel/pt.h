#pragma once

#include <aarch64/mmu.h>

struct pgdir {
    PTEntriesPtr pt;
};

void init_pgdir(struct pgdir *pgdir);
void free_pgdir(struct pgdir *pgdir);
void attach_pgdir(struct pgdir *pgdir);
// Lookup/create PTE for VA (returns pointer to L3 entry)
PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc);

// Debug: dump page-table walk for a VA in given pgdir
void pt_dump_va(struct pgdir *pgdir, u64 va);

// Debug helper: check W^X and exec permission for a given VA
void pt_check_user_exec_violation(struct pgdir *pgdir, u64 va);
