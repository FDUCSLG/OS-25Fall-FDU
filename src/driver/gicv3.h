#pragma once

#include <common/defines.h>

void gicv3_init(void);
void gicv3_init_percpu(void);
void gic_eoi(u32 iar);
u32 gic_iar(void);
bool gic_enabled(void);
