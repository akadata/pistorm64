; bootnode structure:
;    dc.l $00000000          ; LN_SUCC
;    dc.l $00000000          ; LN_PRED
;    dc.b $10                ; LN_TYPE - NT_BOOTNODE
;    dc.b $00                ; LN_PRI
;    dc.l $00000000          ; LN_NAME
;    dc.w $0000              ; BN_FLAGS
;    dc.l $00000000          ; BN_DEVICENODE

; d0 = boot priority
; a0 = device node
; a1 = config dev
; a6 = expansion base
add_boot_node:
    movem.l d2/a2-a4/a6,-(sp)     ; rescue regs
    movea.l a0,a2                 ; device node
    movea.l a1,a3                 ; config dev
    lea.l   74(a6),a4             ; eb_Mountlist
    move.l  d0,d2                 ; boot priority
    movea.l $00000004,a6          ; exec base
    moveq   #20,d0                ; bootnode size
    move.l  #$010000,d1           ; alloc mem attributes (MEM_ANY, MEM_CLEAR)
    jsr     -$C6(a6)              ; exec.library/AllocMem()
    tst.l   d0                    ; check if we got memory
    beq.s   alloc_failed          ; and return in case of failure
    movea.l a4,a0                 ; eb_Mountlist
    movea.l d0,a1                 ; bootnode, fill bootnode:
    move.b  #$10,8(a1)            ; set type (NT_BOOTNODE)
    move.b  d2,9(a1)              ; set priority
    move.l  a3,10(a1)             ; set config dev
    move.l  a2,16(a1)             ; set device node
    jsr     -$10E(a6)             ; exec.library/Enqueue()
    moveq   #1,d0                 ; success return value
alloc_failed:
    movem.l (sp)+,d2/a2-a4/a6     ; restore regs
    rts
