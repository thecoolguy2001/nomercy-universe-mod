/* Resident module at 0x80410000 (Expansion Pak RAM, unused by No Mercy).
 * .bss is placed inside the loaded image so the binary carries its zeros. */
OUTPUT_ARCH(mips)
ENTRY(hook_entry)

SECTIONS
{
    . = 0x80410000;
    .image : {
        KEEP(*(.text.entry))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*) *(.sdata .sdata.*)
        *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
        . = ALIGN(16);
    }
    __resident_end = .;
    ASSERT(__resident_end <= 0x80480000, "resident module too large")
    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment)
                  *(.gnu.attributes) *(.mdebug*) *(.eh_frame*) }
}
