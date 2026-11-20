# Replaces No Mercy's entry code at ROM 0x1000 / RAM 0x80000400 (max 96 bytes).
#
# Copies the Universe payload from the end of the ROM into the game's
# not-yet-used BSS area and jumps to it. The payload later restores the
# original entry code and jumps to it, which clears the BSS (wiping the
# payload) and boots the game exactly as before.
#
# Build defines: PAYLOAD_CART (PI address), PAYLOAD_LEN (bytes, multiple of 8)

    .set noreorder
    .set noat
    .text
    .globl stub
stub:
    lui     $t0, 0xA460                     # PI registers
    lui     $t1, 0x0006                     # RDRAM 0x00060000 (= 0x80060000)
    sw      $t1, 0x00($t0)                  # PI_DRAM_ADDR
    lui     $t1, (PAYLOAD_CART >> 16)
    ori     $t1, $t1, (PAYLOAD_CART & 0xFFFF)
    sw      $t1, 0x04($t0)                  # PI_CART_ADDR
    lui     $t1, ((PAYLOAD_LEN - 1) >> 16)
    ori     $t1, $t1, ((PAYLOAD_LEN - 1) & 0xFFFF)
    sw      $t1, 0x0C($t0)                  # PI_WR_LEN: start cart -> RDRAM DMA
1:  lw      $t2, 0x10($t0)                  # PI_STATUS
    andi    $t2, $t2, 3                     # DMA or IO busy?
    bnez    $t2, 1b
    nop
    ori     $t2, $zero, 2
    sw      $t2, 0x10($t0)                  # clear PI interrupt
    lui     $sp, 0x800B                     # payload stack top 0x800BE000
    ori     $sp, $sp, 0xE000
    lui     $t3, 0x8006
    jr      $t3                             # payload _start at 0x80060000
    nop
