# Payload entry point, linked at 0x80060000 (inside the game's BSS area).
    .set noreorder
    .section .text.start, "ax"
    .globl _start
_start:
    # Clear our .bss through uncached KSEG1 addresses so no cache lines are
    # left dirty for memory that DMA (controller buffer) will write.
    la      $t0, __bss_start
    la      $t1, __bss_end
    lui     $t2, 0x2000
    or      $t0, $t0, $t2
    or      $t1, $t1, $t2
1:  beq     $t0, $t1, 2f
    nop
    sw      $zero, 0($t0)
    b       1b
    addiu   $t0, $t0, 4
2:  jal     n64_main                        # never returns
    nop
3:  b       3b
    nop

    # The 96 bytes of No Mercy's original entry code, extracted from the
    # untouched ROM by tools/build_rom.py at build time.
    .section .rodata
    .balign 4
    .globl orig_entry
orig_entry:
    .incbin "build/n64/orig_entry.bin"

    # Resident module image (built from n64/resident/), copied to 0x80410000.
    .section .rodata
    .balign 4
    .globl resident_image
resident_image:
    .incbin "build/n64/resident.bin"
resident_image_end:
    .balign 4
    .globl resident_image_size
resident_image_size:
    .word resident_image_end - resident_image
