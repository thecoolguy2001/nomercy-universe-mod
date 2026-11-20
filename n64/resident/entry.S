# Resident module entry points, at the very start of the image (0x80410000).
# The ROM patch at osContGetReadData (0x800346A8) is "j 0x80410000; nop",
# so the first instruction here must be the hook. The game's caller's
# return address is still in $ra, so the C hook returns straight to it.
    .set noreorder
    .section .text.entry, "ax"
    .globl hook_entry
hook_entry:
    j       hook_cont_get_read_data
    nop
