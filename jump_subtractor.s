@ jump_subtractor.s

/*function to remove jumps from the total*/
.global useJump

useJump:
    mov r1, #1
    sub r0, r0, r1
    mov pc, lr
