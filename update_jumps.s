.global jumpcount
update_jumps:
    str lr, [sp, #-4]!
    CMP r0, #0
    BEQ no_jumps_left
    SUB r0, r0, #1
no_jumps_left:
    ldr lr, [sp], #4
    BX lr
