#pragma once

#define EXTMEM 0x40000000
#define PHYSTOP 0x80000000

#define KSPACE_MASK 0xFFFF000000000000
#define KERNLINK (KSPACE_MASK + EXTMEM) /* Address where kernel is linked */

#define K2P_WO(x) ((x) - (KSPACE_MASK)) /* Same as V2P, but without casts */
#define P2K_WO(x) ((x) + (KSPACE_MASK)) /* Same as P2V, but without casts */
