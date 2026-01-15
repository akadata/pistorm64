
 IFD MAKESOLO 
SOLO	= 1
 ELSE
SOLO	= 0
 ENDC
	; 0 = MULTI compilieren, 1 = SOLO

VERSION_MAIN	= 5
VERSION_REV	= 3
VERSION_REV2	= 0

;GER	= 1	; Definiert = Deutsch , kommentieren für englisch

DEVPAC	= 0


DATASEG	MACRO
	SECTION	"XData",DATA,CHIP
	ENDM
CODESEG	MACRO
	SECTION "XCode",CODE
	ENDM
BSSSEG	MACRO
 IFNE SOLO
	SECTION "XData",DATA,CHIP
 ELSE
	SECTION "XVars",BSS
 ENDC
	ENDM

NIBBLEINFO = 0	; 1 = DEBUG-Info in Speicher ablegen 

 
DOSCOPY	=  0
BAMCOPY	=  1 
DOSPLUS	=  2
NIBBLE	=  3

OPTIMIZE	=  4
FORMAT	=  5
QFORMAT	=  6
ERASE	=  7
MESLEN  	=  8

NAME	=  9
DIR 	= 10
CHECK	= 11
INSTALL = 12
DRIVESON = 22

KILLSYS	= 20
QUIT	= 21


INDEXCOPY	=  $F8BC
BLACK	= 0

;	*** Farben für XCopyPro
;	*** -------------------
	IFEQ SOLO
BPR	= 40	; 320 Pix. => 40 Bytes per Row
DEP	= 5	; 5 Bitplanes

BLUE	= 11
GREEN	= 12
DGREEN	= 13
RED	= 14
YELLOW	= 21
LGREY	= 7
MGREY	= 4
DGREY	= 2

;	*** Farben für SoloCopy
;	*** -------------------
	ELSE

BPR	= 40	; 320 Pix. => 40 Bytes per Row
DEP	= 2	; 2 Bitplanes

BLUE	= 1	;7
RED	= 1	;6
GREEN	= 2	;5

DGREEN	= 2	; wird durch Copperliste gesetzt
YELLOW	= 3

LGREY	= 3	;4
MGREY	= 2	;3
DGREY	= 1	;

	ENDC
	

XPRINTF	MACRO
	pea	\2			; FormatString
	pea	\1			; Ausgabepuffer
	bsr	Format
	lea	(8+\3)(SP),SP		;Stack korrigieren
	ENDM

TON_BAD	MACRO
	move.l	D0,-(SP)
	moveq	#1,D0
	jsr	x_Ton
	move.l (SP)+,D0
	ENDM

TON_OK	MACRO
	move.l	D0,-(SP)
	moveq	#0,D0
	jsr	x_Ton
	move.l	(SP)+,D0
	ENDM

StartTimer MACRO
	move.l	#\1,D0
	bsr	x_StartTimer
	ENDM

Delay	MACRO
	move.l	#\1,D0
	bsr	x_Delay
	ENDM

TestTimer	MACRO 
	bsr	x_TestTimer
	bne	\1
	ENDM
	
VBLWait	MACRO 
	move.w	#\1,D0
	bsr	x_VBLWait
	ENDM

CCOLOR	MACRO
	move.l	d0,-(sp)
	moveq	#-1,d0
.ccl\@	move	d0,$dff180
	dbf	d0,.ccl\@
	move.l	(sp)+,d0
	ENDM
