# Resident module entry points, at fixed addresses at the start of the image.
#   0x80410000  osContGetReadData hook  (ROM: 0x800346A8 -> j 0x80410000)
#   0x80410008  asset lookup hook       (ROM: 0x80003BD0 -> j 0x80410008)
# The game's return address is still in $ra in both cases.
#include "../../build/n64/universe_label.h"
    .set noreorder
    .set noat
    .section .text.entry, "ax"
    .globl hook_entry
hook_entry:
    j       hook_cont_get_read_data
    nop

    .globl asset_lookup_entry
asset_lookup_entry:
    j       asset_lookup_hook
    nop

# Asset lookup (No Mercy 0x80003BD0): a0 = record to fill, a1 = 1-based
# asset id. Record: +0 ROM start, +4 ROM end, +8 unpacked size,
# +0xC compressed length (0 = stored uncompressed).
# The "Records" label (id 0x4491) is redirected to the Universe Mode label.
asset_lookup_hook:
    andi    $t0, $a1, 0xFFFF
    ori     $t1, $zero, 0x4491
    bne     $t0, $t1, 1f
    nop
    lui     $t0, (LABEL_ROM >> 16)
    ori     $t0, $t0, (LABEL_ROM & 0xFFFF)
    sw      $t0, 0x0($a0)
    addiu   $t1, $t0, LABEL_PACKED
    sw      $t1, 0x4($a0)
    ori     $t1, $zero, LABEL_SIZE
    sw      $t1, 0x8($a0)
    ori     $t1, $zero, LABEL_PACKED
    jr      $ra
    sw      $t1, 0xC($a0)
1:  andi    $v0, $a1, 0xFFFF      # the two instructions the ROM patch replaced
    sltiu   $v1, $v0, 0x1
    j       0x80003BD8
    nop
