#pragma once

#define NUM_IRQ_TYPES 64

typedef enum {
    TIMER_IRQ = 27,
    UART_IRQ = 33,
    VIRTIO_BLK_IRQ = 48
} InterruptType;

typedef void (*InterruptHandler)();

void init_interrupt();
void interrupt_global_handler();
void set_interrupt_handler(InterruptType type, InterruptHandler handler);
