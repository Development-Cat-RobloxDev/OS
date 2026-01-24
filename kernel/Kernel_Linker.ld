ENTRY(kernel_entry)

SECTIONS {
    . = 0x10000;

    .text : {
        *(.text*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
    }
}
