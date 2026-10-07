ENTRY(_start)

SECTIONS
{
    . = 1M;

    .multiboot : ALIGN(4)
    {
        KEEP(*(.multiboot))
    }

    .text :
    {
        *(.text)
    }

    .rodata :
    {
        *(.rodata)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        *(COMMON)
        *(.bss)
    }

    /* Discard GCC exception frames to keep the binary clean for GRUB */
    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
    }
}
