
	BASEREG OFF

;	************************************************
;	*** PPDecrunch
;	*** ----------
;	*** a0: packed data
;	*** a1: destination buffer
;	*** d0: packed data length
;	************************************************

PPDecrunch:
	movem.l	d0-d7/a0-a6,-(sp)	;Regs Retten 
	move.l	a0,a2
	add.l	(a2),a0
	;
	bsr.s	.CrunchDings
	movem.l	(sp)+,d0-d7/a0-a6	;Regs back
	rts

.CrunchDings			
	moveq	#32,D6			;Eigentliche Decrunch Routine
	move.l	#3,A3
	move.l	#4,A4
	move.l	#7,A6
 	lea 	.myBitsTable(PC),a5
	move.l	4(a2),(a5)		; for efficiency
	move.l	a1,a2
	move.l	-(a0),d5
	moveq	#0,d1
	move.b	d5,d1
	lsr.l	#8,d5
	add.l	d5,a1
	move.l	-(a0),d5
	lsr.l	d1,d5
	move.b	d6,d7
	sub.b	d1,d7
.LoopCheckCrunch
	bsr.s	.ReadBit
	tst.b	d1
	bne.s	.CrunchedBytes
.NormalBytes
	moveq	#0,d2
.Read2BitsRow
	moveq	#1,d0
	bsr.s	.ReadD1
	add.w	d1,d2
	cmp.w	d1,a3
	beq.s	.Read2BitsRow
.ReadNormalByte
	moveq	#7,d0
	bsr.s	.ReadD1
	move.b	d1,-(a1)
	dbf	d2,.ReadNormalByte
	cmp.l	a1,a2
	bcs.s	.CrunchedBytes
	rts
.CrunchedBytes
	moveq	#1,d0
	bsr.s	.ReadD1
	moveq	#0,d0
	move.b	(a5,d1.w),d0
	move.l	d0,d4
	move.w	d1,d2
	addq.w	#1,d2
	cmp.w	d2,a4
	bne.s	.ReadOffset
	bsr.s	.ReadBit
	move.l	d4,d0
	tst.b	d1
	bne.s	.LongBlockOffset
	moveq	#7,d0
.LongBlockOffset
	bsr.s	.Read11
	move.w	d1,d3
.Read3BitsRow
	moveq	#2,d0
	bsr.s	.ReadD1
	add.w	d1,d2
	cmp.w	d1,a6
	beq.s	.Read3BitsRow
	bra.s	.DecrunchBlock
.ReadOffset
	bsr.s	.Read11
	move.w	d1,d3
.DecrunchBlock
	move.b	(a1,d3.w),d0
	move.b	d0,-(a1)
	dbf	d2,.DecrunchBlock
.EndOfLoop
	cmp.l	a1,a2
	bcs.s	.LoopCheckCrunch
	rts
.ReadBit
	moveq	#1,d0
.Read11
	subq.w	#1,d0
.ReadD1
	moveq	#0,d1
.ReadBits
	lsr.l	#1,d5
	roxl.l	#1,d1
	subq.b	#1,d7
	bne.s	.No32Read
 	move.b	d6,d7
	move.l	-(a0),d5
.No32Read
	dbf	d0,.ReadBits
	rts

.myBitsTable
	DC.L 0			; pack rate (hier nix eintragen !!!)


	BASEREG A4
