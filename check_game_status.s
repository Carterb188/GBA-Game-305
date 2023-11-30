.global check_game_status
check_game_status:
    sub sp, sp, #8
    str lr, [sp, #4]
    str r4, [sp]
    ldr r2, [r0]
    cmp r2, #0
    movge r4, #1
    movlt r4, #0
    str r4, [r1]
    ldr r4, [sp]
    ldr lr, [sp, #4]
    add sp, sp, #8
    bx lr
