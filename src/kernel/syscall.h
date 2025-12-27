#include <kernel/syscallno.h>
#include <kernel/proc.h>

#define NR_SYSCALL 512

void syscall_entry(UserContext *context);