
	INCDIR	"devel:as/include"

	INCLUDE	"offsets.i"
	INCLUDE	"macros.i"

	INCLUDE	"dos/dos.i"
	INCLUDE	"dos/dosextens.i"
	INCLUDE	"intuition/intuition.i"


GER = 1


 IFEQ GER
	OUTPUT	"XCopy_Config"
 ELSE
 	OUTPUT	"XCopy_Konfig"
 ENDC

XCOPYSIZE = 100000

;-----------------------------------------------------------------------
	CODE
;-----------------------------------------------------------------------

run:	sub.l	A1,A1		; FindTask(0)
	CALLSYS	FindTask
	move.l	D0,A1		; Pointer auf eigenen Prozess
	tst.l	pr_CLI(A1)	; wird XCOPY vom CLI gerufen
	bne.s	.cli
	;
	move.l	A1,-(SP)	; XCOPY wird von WorkBench gerufen
	lea	pr_MsgPort(A1),A0
	CALL	WaitPort	; bei Workbench-Startup auf Message warten
	move.l	(SP)+,A1
	lea	pr_MsgPort(A1),A0
	CALL	GetMsg
	move.l	D0,wbmsg	; Pointer auf Startup-Message retten
	;
.cli	bsr.s	main
	;
	move.l	d0,-(sp)
	tst.l	wbmsg
	beq.s	.exit		; vom CLI gestartet
	CALLSYS	Forbid
	move.l	wbmsg(pc),A1	; Workbench-Message zurueckschicken
	CALL	ReplyMsg
	;
.exit	move.l	(sp)+,d0
	rts

main:	OPENDOS
	OPENINT
	OPENGFX

	lea	windef,a0
	CALLINT	OpenWindow
	move.l	d0,window
	beq	exit

	lea	loadingmsg(pc),a0
	bsr	Status

;	*** RUN ***

	move.l	#xcopyname,d1		; load
	CALLDOS	LoadSeg
	move.l	d0,seglist
	beq	err_open

	move.l	d0,-(sp)
	lea	runningmsg(pc),a0
	bsr	Status
	move.l	(sp)+,d0

	lsl.l	#2,d0			; run
	move.l	d0,a0
	move.w	#"><",6(a0)
	move.l	a0,segdata
	jsr	4(a0)


	bsr	seek1

	move.l	seglist,d1		; unload
	CALLDOS	UnLoadSeg


	lea	readingmsg(pc),a0
	bsr	Status

	move.l	#xcopyname,d1		; open for read
	move.l	#MODE_OLDFILE,d2
	CALLDOS	Open
	move.l	d0,file
	beq	err_open

	move.l	file(pc),d1		; read
	move.l	#xcopydata,d2
	move.l	#XCOPYSIZE,d3
	CALL	Read

	move.l	file(pc),d1
	CALL	Close

	bsr	seek2
	bne	err_seek

.found	move.l	(a0)+,d0
	move.l	a0,offset
	sub.l	#xcopydata,offset

	lea	patchingmsg(pc),a0
	bsr	Status

	move.l	#xcopyname,d1			; open xcopy for write
	move.l	#MODE_READWRITE,d2
	CALLDOS	Open
	move.l	d0,file
	beq	err_open

	move.l	file(pc),d1			; seek
	move.l	offset(pc),d2
	move.l	#OFFSET_BEGINNING,d3
	CALL	Seek

	move.l	file(pc),d1			; write
	move.l	#defdata,d2
	move.l	defsize,d3
	CALL	Write

	move.l	file(pc),d1			; close
	CALL	Close

	lea	okmsg(pc),a0
	bsr	Status

ende	move.l	window,a0		; wait for closewindow
	move.l	wd_UserPort(a0),a0
	CALLSYS	WaitPort
	move.l	window,a0
	move.l	wd_UserPort(a0),a0
	CALL	GetMsg
	move.l	d0,a1
	CALL	ReplyMsg

	move.l	window,a0		; close window
	CALLINT	CloseWindow


exit	CLOSEGFX			; shut down
	CLOSEDOS
	CLOSEINT
	moveq	#0,d0
	rts


;	*** seek1
;	*** -----
;	*** copy defaults from loadseg'd xcopy to defdata
;	******************************************
seek1:	move.l	segdata,a0		; seek
	lea	4+2+2(a0),a0
	move.l	(a0)+,d0
	move.l	d0,defsize
	subq.l	#1,d0
	lea	defdata,a1
.copy	move.b	(a0)+,(a1)+
	dbf	d0,.copy
	moveq	#0,d0
	rts


;	*** seek2
;	*** -----
;	*** find $DEADC0DE in xcopy executable
;	******************************************
seek2:	lea	xcopydata,a0		; seek
	move.l	#$DEADC0DE,d0
	move.w	#XCOPYSIZE/4-1,d1
.seek	cmp.l	(a0)+,d0
	beq.s	.found
	dbf	d1,.seek
	bra	.err
.found	moveq	#0,d0
	rts
.err	moveq	#-1,d0
	rts


;	*** err_xxx
;	*** -------
;	*** error messages
;	******************************************
err_open:
	lea	openerrmsg(pc),a0
err	bsr	Status
	bra	ende
err_seek:
	lea	seekerrmsg(pc),a0
	bra	err


;	*** Status
;	*** ------
;	*** a0:message
;	******************************************

Status:	move.l	a2,-(sp)
	move.l	a0,a2

	move.l	window(pc),a1
	move.l	50(a1),a1
	moveq	#20,d0
	moveq	#28,d1
	CALLGFX	Move

	move.l	window(pc),a1
	move.l	50(a1),a1
	moveq	#1,d0
	CALL	SetAPen

	move.l	window(pc),a1
	move.l	wd_RPort(a1),a1
	move.l	a2,a0
	moveq	#-1,d0
.len	addq.l	#1,d0
	tst.b	(a2)+
	bne	.len
	CALL	Text
	move.l	(sp)+,a2
	rts

;-----------------------------------------------------------------------

	CNOP 0,4
file		DC.L 0
seglist		DC.L 0
defsize		DC.L 0
offset		DC.L 0
segdata		DC.L 0
window		DC.L 0
wbmsg		DC.L 0

windef
	DC.W 20,20,300,60
	DC.B 2,1
	DC.L CLOSEWINDOW,WINDOWDRAG!WINDOWDEPTH!ACTIVATE!RMBTRAP!WINDOWCLOSE
	DC.L 0,0,wintitle
	DC.L 0,0
	DC.W 20,20,300,100
	DC.W WBENCHSCREEN


 IFEQ GER

wintitle	DC.B "X-Copy Configuration",0
loadingmsg	DC.B "Loading X-Copy...      ",0
runningmsg	DC.B "Running X-Copy...      ",0
readingmsg	DC.B "Reading X-Copy...      ",0
patchingmsg	DC.B "Patching X-Copy...     ",0
okmsg		DC.B "Done.                  ",0
openerrmsg	DC.B "Error opening X-Copy!  ",0
seekerrmsg	DC.B "Illegal X-Copy Version!",0

 ELSE

wintitle	DC.B "X-Copy Konfiguration   ",0
loadingmsg	DC.B "Lade X-Copy...         ",0
runningmsg	DC.B "Starte X-Copy...       ",0
readingmsg	DC.B "Lese X-Copy...         ",0
patchingmsg	DC.B "Ändere X-Copy...       ",0
okmsg		DC.B "Fertig                 ",0
openerrmsg	DC.B "Fehler beim Öffnen!    ",0
seekerrmsg	DC.B "Falsche X-Copy Version!",0

 ENDC

xcopyname	DC.B "XCopyPro",0
		DS.B 20
		EVEN


;-----------------------------------------------------------------------
	BSS
;-----------------------------------------------------------------------

xcopydata	DS.B XCOPYSIZE
defdata		DS.B 100

;-----------------------------------------------------------------------
	END
