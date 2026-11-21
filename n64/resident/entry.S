# Resident module entry points, at fixed addresses at the start of the image.
#   0x80410000  osContGetReadData hook  (ROM: 0x800346A8 -> j 0x80410000)
#   0x80410008  asset lookup hook       (ROM: 0x80003BD0 -> j 0x80410008)
#   0x80410010  play_song hook          (ROM: 0x80017CF4 -> j 0x80410010)
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

    .globl play_song_entry
play_song_entry:
    j       play_song_hook
    nop

# play_song(a0 = song, a1 = mode, a2 = volume) at 0x80017CF4. While the
# mailbox flag music_override (0x80408024) holds a song, menu-music
# requests (songs 1..0x1F) are replaced by it. 0 (silence), -1 (stop) and
# wrestler themes (>= 0x20) pass through unchanged.
play_song_hook:
    lui     $t0, 0x8041
    lw      $t1, -0x7FDC($t0)      # 0x80408024 music_override
    beqz    $t1, 2f
    nop
    blez    $a0, 2f                # 0 or negative: leave alone
    nop
    sltiu   $t2, $a0, 0x20
    beqz    $t2, 2f                # wrestler themes: leave alone
    nop
    move    $a0, $t1
2:  lui     $v0, 0x8005            # the two instructions the ROM patch replaced
    lw      $v0, -0x7A78($v0)
    j       0x80017CFC
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
