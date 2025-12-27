ENTRY(_start)

SECTIONS
{
    . = 0xFFFF000040000000;
    .text.boot : AT(ADDR(.text.boot) - 0xFFFF000000000000) {
      KEEP(*(.text.boot))
    }
    . = ALIGN(4096);
    .text : AT(ADDR(.text) - 0xFFFF000000000000) {
      *(.text) 
      *(.text.*)
    }
    PROVIDE(etext = .);
    . = ALIGN(4096);
    .init : AT(ADDR(.init) - 0xFFFF000000000000) { 
        PROVIDE(early_init = .);
        KEEP(*(.init.early))
        PROVIDE(rest_init = .);
        KEEP(*(.init.rest))
        PROVIDE(init = .);
        KEEP(*(.init))
        PROVIDE(einit = .);
    }
    .rodata : AT(ADDR(.rodata) - 0xFFFF000000000000) {
       *(.rodata)
       *(.rodata.*)
    }
    PROVIDE(data = .);
    .data : AT(ADDR(.data) - 0xFFFF000000000000) {
      *(.data)
      *(.data.*)
    }
    PROVIDE(edata = .);
    .bss : AT(ADDR(.bss) - 0xFFFF000000000000) {
      *(.bss .bss.*) 
    }
    PROVIDE(end = .);
}