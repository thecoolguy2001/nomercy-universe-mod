/*
 * Payload memory map, all inside No Mercy's BSS (0x80056710-0x800BEE90),
 * which the game clears on boot anyway:
 *   0x80060000  code, data, bss       (must end before 0x80090000)
 *   0x80090000  320x240 16-bit framebuffer (0x25800 bytes)
 *   0x800BE000  stack top
 */
OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
    . = 0x80060000;
    .text   : { KEEP(*(.text.start)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) *(.sdata .sdata.*) }
    . = ALIGN(16);
    __payload_end = .;
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    ASSERT(__bss_end <= 0x80090000, "payload too large for its RAM window")

    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment)
                  *(.gnu.attributes) *(.mdebug*) *(.eh_frame*) }
}
