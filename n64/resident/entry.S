# Resident module entry points, at fixed addresses at the start of the image.
#   0x80410000  osContGetReadData hook  (ROM: 0x800346A8 -> j 0x80410000)
#   0x80410008  asset lookup hook       (ROM: 0x80003BD0 -> j 0x80410008)
#   0x80410010  play_song hook          (ROM: 0x80017CF4 -> j 0x80410010)
#   0x80410018  menu text hook          (ROM 0x5AC1C: jal 0x800DDC20 -> jal 0x80410018)
#   0x80410020  text-image hook         (ROM: 0x80009FFC -> j 0x80410020)
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

    .globl text_entry
text_entry:
    j       text_hook
    nop

    .globl text_seen_entry
text_seen_entry:
    j       text_seen_hook
    nop

# get_text_image(a0 = string | flags, a1 = style) at 0x80009FFC: every
# piece of on-screen text passes through here. Tell the director which
# strings are being drawn (used to recognise the highlighted wrestler),
# then continue into the original routine.
text_seen_hook:
    addiu   $sp, $sp, -0x28
    sw      $ra, 0x24($sp)
    sw      $a0, 0x18($sp)
    sw      $a1, 0x1C($sp)
    sw      $a2, 0x20($sp)
    jal     text_seen
    sw      $a3, 0x14($sp)
    lw      $ra, 0x24($sp)
    lw      $a0, 0x18($sp)
    lw      $a1, 0x1C($sp)
    lw      $a2, 0x20($sp)
    lw      $a3, 0x14($sp)
    addiu   $sp, $sp, 0x28
    addiu   $sp, $sp, -0x50       # the two instructions the ROM patch replaced
    sw      $22, 0x48($sp)
    j       0x8000A004
    nop

# Replaces the menu's per-frame draw_text(holder, str, x, y, alpha, style,
# flags) call (0x800DDC20, called from 0x800DE6FC). Draws the original text,
# then lets Universe Mode draw its own lines into the same display list.
text_hook:
    addiu   $sp, $sp, -0x40
    sw      $ra, 0x3C($sp)
    sw      $a0, 0x20($sp)
    lw      $t0, 0x50($sp)          # caller's stack arguments 0x10..0x18
    sw      $t0, 0x10($sp)
    lw      $t0, 0x54($sp)
    sw      $t0, 0x14($sp)
    lw      $t0, 0x58($sp)
    sw      $t0, 0x18($sp)
    lui     $t9, 0x800D
    ori     $t9, $t9, 0xDC20
    jalr    $t9
    nop
    jal     universe_draw
    lw      $a0, 0x20($sp)
    lw      $ra, 0x3C($sp)
    jr      $ra
    addiu   $sp, $sp, 0x40

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
