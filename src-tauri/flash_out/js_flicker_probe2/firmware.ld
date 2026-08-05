
ENTRY(_start)
SECTIONS {
    . = 0x403B2000;
    .entry.literal : { *(.entry.literal) }
    .entry : { KEEP(*(.entry)) }
    .text : { *(.literal .text .text.*) }
    . = 0x3FC88000;
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.xt.lit) *(.xt.prop) *(.eh_frame) }
}
