 
; ** 13.10.91: OPTIMIZE gefixed. Mit 300k und 2MB freiem Speicher getestet.

; ** 8.11.90 : DiskName wird auf 29 Zeichen beschraenkt, da sonst
; **           bei DiskInfo das Zeilenende der Statuszeile ueberschrieben wird.

; ** 11.12.90: Verify_Error beim RAM-Copy behoben. Wurde Verify auf der
; **           Upper-Side durchgefuert, so war nur noch das letzte Laufwerk
; **           fuer den nachfolgenden Schreibvorgang auf der Lower-Side gesetzt
; **           ( und nicht ALL, wie es sein muestte).       

; ** 11.12.90: Die Funktion ReadBlock (bzw. ReadDecode) bricht den aktuellen
; **           Vorgang bei einem Lesefehler mit einem DosError ab und kehrt
; **           ins Menue zurueck. Bisher wurden Lesefehler weitestgehend 
; **           ignoriert.  
        
; **********************************************************************
; **
; **  alle Symbole, die exportiert werden  

; **  Funktionen	
;
;  XCOPY DOSCOPY/DOSCOPY+ MODULE
;	
;  Date      Who        Changes                                           
;  --------- ---------- --------------------------------------------------
;  16-MAR-89 Frank      komplette Ueberarbeitung
;  20-MAR-89 Frank      Fast Format 	
;  27-DEC-89 Frank      Stack-Fehler beim Verify des RamCopy`s behoben
;

;---------------------------------------------------------------------------
; ** Ein paar LVO Offsets
 IFND _DoIO
_DoIO = -456
_OpenDevice = -444
_CloseDevice = -450
_AddPort = -354
_RemPort = -360
_FindTask = -294
 ENDC

	BSSSEG
;	DATASEG

source	DS.W	1
target	DS.W	1
all	DS.W	1
track	DS.W	1
head	DS.W	1	
tries	DS.W	1
disklen	DS.W	1
flag	DS.W	1	; ist nur ein Byte!

actdriv DS.W	1

ptr     DS.L	1
mfm1	DS.L	1
mfm2	DS.L	1
mfm3	DS.L	1
ver1    DS.L	1
ver2    DS.L	1
ver3    DS.L	1
stack   DS.L	1
err1    DS.W	1
err2    DS.W	1
TMP	DS.W	1
FLG	DS.W	1

s_track	DS.W	1
s_head	DS.W	1
LowMem	DS.L	1
RamVec	DS.L	1
DriStat	DS.L	1

	CODESEG
;---------------------------------------------------------------------------
;
;                        Tabelle der Rueckmeldungen
;
Complete
	moveq	#0,D0
	bra.s	goout

SpecialERR
	moveq	#1,D0
	bra.s	goout

UserBreak:
	moveq	#0,d0
	move.w	d0,$D8(A6)
	move.w	d0,$d4(a6)
	move.w	#$0008,$96(A6)
	moveq	#2,D0
	bra.s	goout

NoIndex	lsl.l	#8,D0	; noch  nicht implementiert
	addq.l	#3,D0
	rts

VErr	lsl.l	#8,D0	; in D0 steht Drive 0,1,2,3!!
	addq.l	#4,D0
	bra.s	goout

wprotON	lsl.l	#8,D0	; in D0 steht Drive 0,1,2,3!!
	addq.l	#5,D0
	bra.s	goout

NoDrive	lsl.l	#8,D0	; in D0 steht Drive 0,1,2,3!!
	addq.l	#6,D0
	bra.s	goout

OptError
	lsl.l	#8,D0	; in D0 steht Fehler von Optimize
	addq.l	#7,D0
	bra.s	goout

NoMemory
	moveq	#8,D0
;	bra.s	goout

goout	bra	out

;----------------------------------------------------------------------------

Start:	move.l	A7,stack	; Stackpth. fuer OUT
	moveq	#0,D0
	move.b	Source,D0
	move.w	D0,source
	move.b	Target,D0
	move.w	D0,target
	move.w	source,D0	; Default
 	or.w	target,D0
	move.w	D0,all		; alle selektierten Drives

	moveq	#0,D0		; loeschen ab Track0
	move.w	D0,track
	move.w	D0,s_head
	bsr	StatClear	; Trackanzeige loeschen
	move.w	starttrack,s_track
	move.w	starthead,s_head
	clr.l	SYSTime
	move.w	#1,copy_active	; RamHandler aktivieren	

	bsr	MFMAlloc	; Speicher holen
	tst.w	TMP
	bne	Driver		; RAM-COPY

	move.w	mode,D0
	cmp.w	#CHECK,D0
	beq	no_target
	cmp.w	#DIR,D0
	beq	no_target
	cmp.w	#NAME,D0
	bne	sou_chk
no_target
	clr.w	target		; Target deselektieren bei CHECK+NAME+DIR
	move.w	source,all	; nur Source positionieren
	bra	go_pos

sou_chk	cmp.w	#ERASE,D0
	beq.s	no_source
	cmp.w	#OPTIMIZE,D0
	beq.s	no_source
	cmp.w	#MESLEN,D0
	beq.s	no_source
	cmp.w	#FORMAT,D0
	beq.s	fillAAAA
	cmp.w	#QFORMAT,D0
	beq.s	gototrk0
	cmp.w	#INSTALL,d0
	bne.s	go_pos

gototrk0
	clr.w	s_head		; bei QFormat immer Positionierung auf track0
	clr.w	s_track
fillAAAA
	move.l	#$1600/8,D0	; mfm2 mit Daten fuellen und nach mfm1 codieren
	bsr	fillmfm2
	cmp.w	#INSTALL,mode
	beq	no_source
	bsr	Input

no_source
	clr.w	source		; source deselektieren bei ERASE und FORMAT
	move.w	target,all	; Erase nur auf Target Drives

go_pos	moveq	#3,D3		; alle angewaehlten Drives positionieren
	moveq	#1,D4
zaza	move.w	all,D0
	and.l	D4,D0
	beq	notcop		; Drive nicht selected
	bsr	drive
	beq.s	okdsk
	moveq	#3,D0
	sub.l	D3,D0
	bra	NoDrive
	;
okdsk	bsr	track0		; auf Track 0 positionieren
	move.w	target,D0	; Target auf Schreibschutz testen
	and.l	D4,D0
	beq.s	notcop
	btst	#3,$bfe001	; Destination write protected ??
	bne.s	notcop
	moveq	#3,D0
	sub.l	D3,D0
	bra	wprotON
notcop	asl.l	#1,D4
	dbf	D3,zaza

	move.w	s_track,track	; Alle selektierten Drives auf
	move.w	track,D4	; Starttrack fahren
	beq	staypos
nxtstep	bsr	stepall		; Stept die Drives einzeln
	bsr	stepdelay	; Delay fuer alle
	subq.l	#1,d4
	bne.s	nxtstep

staypos	bsr	bigdelay	; stabilisieren aller Laufwerke
	clr.w	err1
	clr.w	err2
	move.w	mode,D0
	cmp.w	#DOSPLUS,D0
	ble	FastCopy	; DOSCOPY + BAMCOPY + DOSPLUS
	cmp.w	#CHECK,D0
	beq	CheckDisk
	cmp.w	#NAME,D0
	beq	DiskInfo
	cmp.w	#DIR,D0
	beq	ShowDir
	cmp.w	#OPTIMIZE,D0
	beq	Optimize
	cmp.w	#MESLEN,D0
	beq	SpeedCheck
	cmp.w	#QFORMAT,D0
	beq	QFormat
	cmp.w	#INSTALL,d0
	beq	QFormat
	bra	Selektor	; Mode = NIBBLE,FORMAT,ERASE
; **
; ** Exit-Routine fuer das gesamte Modul
; ** Gibt alle belegeten Puffer frei, positioniert Drives auf Track0
; ** 
out:	clr.w	copy_active	; STOP
	move.l	stack,SP	; Out kann jederzeit angesprungen werden,
	move.l	D0,-(SP)	; da STACK korrigiert wird
	lea	$DFF000,A6	
	move.w	#$4000,$24(A6)
	bsr	timer_off
	bsr	ClrMEMTAB
	bsr	MFMFree
	tst.w	TMP
	bne.s	.nozero
	;
	moveq	#3,D3	; alle Drives auf TRACK0 positionieren
	moveq	#1,D4
.loop	move.w	all,D0
	and.l	D4,D0
	beq.s	.next	; Drive nicht selected
	bsr	seldri
	bsr	track0	; auf Track 0 positionieren
.next	asl.l	#1,D4
	dbf	D3,.loop
	;
.nozero	bsr	DrivesOFF
	move.l	(SP)+,D0
	rts

;---------------------------------------------------------------------------

bamtest:
	move.l	A5,-(SP)
	cmp.w	#BAMCOPY,mode
	bne.s	.nobam
	move.l	WorkTab,a5
	move.w	track,d0
	asl.w	#1,d0
	add.w	head,d0
	cmp.b	#11,0(a5,d0.W)
	bne.s	.nobam
	move.l	(SP)+,A5
	moveq	#1,d0
	rts
	;
.nobam	move.l	(SP)+,A5
	clr.l	d0
	rts


WaitFlag
	move.l	D0,-(SP)
 	StartTimer 917000	; Maximalwert

wflg	tst.b	flag
	beq.s	flagrdy
	TestTimer wflg

	move.l	(SP)+,D0
	move.w	#$0000,$24(A6)
	moveq	#1,D1
	rts

flagrdy	Delay 500		; 500 us warten zum DMA ausschwiengen
	move.l	(SP)+,D0
	moveq	#0,D1
	rts

FUPPER 	clr.w	head
	bsr	bamtest
	bne	tbreak
	bsr	side0		; Copy only side0		
	bra.s	t1
FLOWER	move.w	#1,head
	bsr	bamtest
	bne	tbreak
	bsr	side1
	clr.w	s_head
t1	move.l	mfm1,A0
	move.l	A0,ptr
	bsr	FastRead
	bsr	WaitFlag
	beq.s	1$
	clr.l	(A0)		; Puffer ungueltig
1$	bsr	sortbuffer	; D0 != 0 => ERRORNumber in D0 !
	move.l	D0,D3
	bsr	StatOut
	tst.l	D3
	beq.s	t3
	move.l	mfm1,A0		; aufgetretenen Lesefehler behandeln
	bsr	ErrorHandler
	move.w	D0,err1
	move.l	ptr,ver3
	bra.s	t4
t3	move.w	target,D0
	bsr	seldri
	move.l	ptr,A0
	move.l	A0,ver3
	bsr	FastWrite
	bsr	WaitFlag
t4	moveq	#1,D0		; Verify nur fuer aktuelle Seite
	bsr	FastVerify
	bra	tbreak
	
;	*** FAST COPY
;	*** ---------
FastCopy:
	tst.w	s_track
	bne.s	fastcopyloop
	tst.w	s_head
	bne.s	fastcopyloop
	bsr	VirusCheck
fastcopyloop
	move.w	source,D0
	bsr	seldri
	move.w	side,D0
	beq	FBOTH
	cmp.w	#1,D0
	beq	FUPPER
	bra	FLOWER

FBOTH	tst.w	s_head		; auf ersten Cylinder nur side1
	bne	FLOWER		; kopieren
	move.w	track,D0
	cmp.w	endtrack,D0
	blt.s	fok0
	tst.w	endhead		; auf letzten Cylinder nur side0
	beq	FUPPER		; kopieren

fok0	clr.w	head
	bsr	bamtest 
	bne.s	fok1
	bsr	side0
	move.l	mfm1,A0
	bsr	FastRead
	bsr	WaitFlag
	beq	fok1
	clr.l	(A0)		; Buffer1 ungueltig => No Sync
	;
fok1	move.w	#1,head
	bsr	bamtest
	bne.s	sortp1
	bsr	side1
	move.l	mfm2,A0
	bsr	FastRead
	;
sortp1	clr.w	head		
	bsr	bamtest
	bne	sortp2
	move.l	mfm1,ptr	; Puffer 1 sortieren
	bsr	sortbuffer	; D0 != 0 => ERRORNumber in D0 !
	move.w	D0,err1
	move.l	D0,D3
	bsr	StatOut
sortp2
	bsr	WaitFlag	; Wait for reading Side1
	beq	fok2
	move.l	mfm2,A0
	clr.l	(A0)		; Puffer2 ungueltig => No Sync

fok2:	move.w	target,D0
	bsr	seldri
	bsr	side0
	tst.w	err1
	bne.s	fnx3
	move.l	ptr,A0
	move.l	A0,ver1
	bsr	bamtest
	bne.s	fnx3
	bsr	FastWrite	; Puffer 1 schreiben, wenn kein Fehler
	;
fnx3	move.w	#1,head		
	move.l	mfm2,ptr	; Puffer 2 sortieren
	bsr	bamtest
	bne.s	xy1
	bsr	sortbuffer
	move.w	D0,err2
	move.l	D0,D3
	bsr	StatOut
xy1
	bsr	WaitFlag	; gegebenenfalls Schreiben Puffer1 abwarten
	tst.w	err2
	bne.s	fnx4
	bsr	side1
	move.l	ptr,A0
	move.l	A0,ver2
	bsr	bamtest
	bne.s	fnx4
	bsr	FastWrite	; Puffer 2 schreiben, wenn kein Fehler
	bsr	WaitFlag

fnx4	tst.w	err1		; aufgetretene Lesefehler behandeln
	beq.s	6$
	bsr	side0
	clr.w	head 
	move.l	mfm1,A0
	bsr	ErrorHandler
	move.w	D0,err1
	move.l	ptr,ver1

6$	tst.w	err2
	beq.s	7$
	bsr	side1
	move.w	#1,head
	move.l	mfm2,A0
	bsr	ErrorHandler
	move.w	D0,err2
	move.l	ptr,ver2

7$	moveq	#0,D0		; D0=0 => Verify fuer beide Seiten
	bsr	FastVerify

tbreak	bsr	BreakStep
	bra	fastcopyloop


;	*** ERRORHANDLER
;	***-----------------------------------------------------
ErrorHandler:
	move.l	A0,ptr		; Pointer in Lesepuffer setzen
	move.w	#2,tries	; 2 weitere Leseversuche
	move.w	source,D0
	bsr	seldri
ehand1	move.l	ptr,A0
	bsr	FastRead
	bsr	WaitFlag
	beq.s	1$
	bsr	nosync		; Buffer1 ungueltig => No Sync
	bra.s	2$ 
1$	bsr	sortbuffer	; D0 != 0 => ERRORNumber in D0 !
	tst.w	D0
	beq.s	3$
2$	subq.w	#1,tries
	bne	ehand1
3$	move.l	D0,D3
	bsr	StatOut		
	tst.l	D3
	beq.s	ok_write
	;
	;*** Fehler aufgetreten!
	;
	cmp.w	#DOSCOPY,mode	; wenn DOSPLUS oder BAMCOPY, dann repair
	beq.s	nib_it		; ansonsten Track nibblen
	move.l	ptr,A1		; REPAIR
	move.l	mfm3,A0
	bsr	DecodeTrack
	move.l	ptr,A1
	move.l	mfm3,A0
	bsr	CodeTrack
	;
ok_write
	move.w	target,D0
	bsr	seldri
	move.l	ptr,A0
	bsr	FastWrite	; Puffer 1 schreiben, wenn kein Fehler
	bsr	WaitFlag
	moveq	#0,D0	; OK
	rts
	;
nib_it	move.l	D3,-(A7)
	bsr	NibbleTrack	; als letzte Moeglichkeit Track nibblen
	move.l	(A7)+,D0	; Rueckmeldumg nach D0
	rts



AskReadError:
	movem.l	d0-d7/a0-a5,-(SP)
	moveq	#0,D0
	move.w	source,D0
	bsr	Bit2Num			; 1,2,4,8 => 0,1,2,3
	or.b	#$30,D0			; DriveNr.
	lea	.readdrvtxt(pc),A0
	move.b	D0,(a0)			; DriveNR in Text schreiben
	moveq	#RED,D0
	lea	.readerrtxt(pc),a0
	bsr	Status
	movem.l	(SP)+,D0-D7/A0-A5
	rts
	;
 IFD GER
.readerrtxt	DC.B "Lesefehler auf DF"
 ELSE
.readerrtxt	DC.B "Read error on DF"
 ENDC
.readdrvtxt	DC.B "x: !",0
		EVEN

;-------------------------------------------------------------------------------
;
;	Pointer auf Vergleichspuffer in ver1,ver2,ver3
;	D0 = 0 Verify fuer beide Seiten, sonst nur aktuelle Seite
;
FastVerify:
	tst.b	Verify
	beq	rtrn
	move.l	D0,-(SP)	
	moveq	#3,D2
	moveq	#1,D1
test	move.l	D1,D0
	and.b	Verify,D0
	beq	nextdri

	move.b	d0,actdriv
	movem.l	D1-D2,-(SP)
	bsr	seldri	; Drive in D0 selektieren
	tst.l	8(SP)
	beq	fv0	; beide Seiten Verify
	bsr	bamtest
	bne	fv2
	bsr	compare	; Verify nur fuer aktuelle Seite
	bra	fv2

fv0	tst.w	err1
	bne	fv1	; Side 0 wurde genibbelt
	bsr	side0
	clr.w	head
	move.l	ver1,ver3
	bsr	bamtest
	bne	fv1
	bsr	compare
fv1	tst.w	err2
	bne	fv2	; Side 1 wurde genibbelt
	bsr	side1
	move.w	#1,head
	move.l	ver2,ver3
	bsr	bamtest
	bne	fv2
	bsr	compare
fv2	movem.l	(SP)+,D1-D2
nextdri	lsl.l	#1,D1	
	dbf	D2,test
	move.l	(SP)+,D0
rtrn	rts	; Alle Drives OK

;vererr	moveq	#$08,D3	; VerifyErrorNumber
;	bsr	StatOut
;	bra	fv2	; kein Abruch
;	movem.l	(SP)+,D1-D2	; Verify Error aufgetreten
;	moveq	#3,D0
;	sub.l	 D2,D0	; Drive in D0
;	bra	VErr	; ACHTUNG Stack!!

compare	move.w	#3,tries
ragain	move.l	mfm3,A0
	move.l	A0,ptr
	bsr	FastRead
	bsr	WaitFlag
	bne	wagain	; No Sync
	bsr	mfmcopy	; schneller als SortPuffer, da kuerzer
	bne	wagain	; Dos Error
	move.l	ptr,A0
	move.l	ver3,A1
	move.w	#$0255,D0
1$	cmp.l	(A0)+,(A1)+
	bne.s	wagain	; 8 Zyklen => kurzer Sprung
	cmp.l	(A0)+,(A1)+
	bne.s	wagain
	cmp.l	(A0)+,(A1)+
	bne.s	wagain
	cmp.l	(A0)+,(A1)+
	bne.s	wagain
	cmp.l	(A0)+,(A1)+
	dbne	D0,1$
	tst.w	D0
	bpl.s	wagain
	moveq	#0,D3	; alles OK 
	bra	StatOut
wagain	subq.w	#1,tries
	bne	retry
	bsr	RetryCancel
	bne	rtrn	; nein, da Z-Flag gesetzt

retry	move.l	ver3,A0
	bsr	FastWrite
	moveq	#8,D3	; VerifyError ausgeben
	bsr	StatOut
	bsr	WaitFlag
	bra	ragain


RetryCancel
	movem.l	d0-d7/a0-a5,-(SP)
	TON_BAD				; Error
	moveq	#0,D0
	move.b	actdriv,D0
	bsr	Bit2Num			; 1,2,4,8 => 0,1,2,3
	or.b	#$30,D0			; DriveNr.
	lea	verrtdr(pc),A0
	move.b	D0,(a0)			; DriveNR in Text schreiben
	moveq.l	#RED,D0
	lea	verrt(pc),a0
	bsr	Status		
	
keylo	bsr	GetKey
	cmp.b	#'R',D0
	beq.s	1$
	cmp.b	#'r',D0
	bne.s	ke1
1$	move.w	#3,tries	; Nochmal 3 Versuche starten	
	bsr	StatusClear
	movem.l	(SP)+,d0-d7/A0-A5
	bra	okret	; Retry, Z-Flag setzen
	
ke1	cmp.b	#'C',D0
	beq.s	1$
	cmp.b	#'c',D0
	bne.s	keylo	
1$	bsr	StatusClear	; Cancel
	movem.l	(SP)+,D0-D7/A0-A5
	bra	ilret	; Cancel, Z-Flag loeschen

 IFD GER
verrt	DC.B	"Verify Fehler auf Disk in DF"
 ELSE
verrt	DC.B	"Verify error on disk in DF"
 ENDC
verrtdr	DC.B	"0: (R/C)",0
	EVEN

;------------------------------------------------------------------------------
FastRead:
	move.w	#$4489,$7E(A6)
	move.w	#$7F00,$9E(A6)
	move.w	#$9500,$9E(A6)
	move.l	A0,$20(A6)		; Diskpointer
	move.w	#$9960,disklen
	bra	startdma

FastWrite:
	move.w	#$7F00,$9E(A6)
	move.w	#$9100,$9E(A6)
	move.w	#$00F9,D0		; Track GAP
1$	move.l	#$aaaaaaaa,-(A0)	; 30 Zyklen pro Durchlauf
	dbf	D0,1$			; 7500 Zyklen <=> 1071 us	
	move.l	A0,$20(A6)
	bsr	minidelay
	move.w	#$D955,disklen
	bra	startdma
;---------------------------------------------------------------------------
;
;                       General Selektor
;
; err1/err2 werden als Flag/Zaehler benutzt !!
;
Selektor:
	move.w	side,D0
	beq.s	MBOTH
	cmp.w	#2,D0
	beq.s	MLOWER
	bra.s	MUPP1	; D0 ist 1, d.h nur Oberseite kopieren

MBOTH	clr.w	err1
	tst.w	s_head	; auf ersten Cylinder nur side1
	bne.s	MLOWER	; kopieren
	move.w	track,D0
	cmp.w	endtrack,D0
	blt.s	MUPPER
	tst.w	endhead	; auf letzten Cylinder AUCH side1 kopieren
	bne.s	MUPPER	
MUPP1	move.w	#1,err1	; nur side0 kopieren
	
MUPPER	clr.w	head
	bsr	side0
	bsr	Decide
	tst.w	err1
	bne	MBreak	; nur side0 kopieren

MLOWER	clr.w	s_head
	move.w	#1,head
	bsr	side1
	bsr	Decide

MBreak	bsr	BreakStep
	bra	Selektor

Decide	move.w	mode,D0	
	cmp.w	#ERASE,D0
	beq	EraseDisk
	cmp.w	#FORMAT,D0
	beq	FFormat
	bsr	NibbleTrack	; NibbleCopy	

NibMesOut:
	moveq	#7,D3	; LONG TRACK
	moveq	#0,D4
	tst.w	D0
	beq.s	2$	; ja
	cmp.w	#RED,D0
	beq.s	1$
	move.w	D0,D4	; Farbe (GREEN,YELLOW,BLUE,LGREY)
	moveq	#0,D3	; NULL ausgeben
	bra	Display
1$	moveq	#2,D3	; NO SYNC FOUND
2$	moveq	#RED,D4	
	bra	Display
;-----------------------------------------------------------------------------

StatClear:
	move.w	track,-(A7)
	move.w	head,-(A7)
	move.w	D0,track	; ab wo loeschen in D0
	tst.w	s_head
	bne.s	2$
1$	moveq	#0,D4	; Farbe Schwarz
	moveq	#0,D3
	clr.w	head
	bsr	Display
2$	moveq	#0,D4	; Farbe Schwarz
	moveq	#0,D3
	move.w	#1,head
	bsr	Display
	addq.w	#1,track
	cmp.w	#81,track
	ble.s	1$
	move.w	(A7)+,head
	move.w	(A7)+,track
	rts

;  D3 = Number, D4 = Color

StatOut	
 IFNE SOLO
	moveq	#2,d4		;solo!
 ELSE
	moveq	#GREEN,D4	; Farbe festlegen GRUEN
 ENDC	
	tst.l	D3	; Error != 0 
	beq	Display	; ROT
	moveq	#RED,D4
	cmp.l	#8,D3
	bls	Display
	moveq	#9,D3
	moveq	#BLUE,D4	; BLUE fuer alle Fehler >= 9 
	bra	Display

;-----------------------------------------------------------------------------
EraseDisk:
	move.w	#$DA00,disklen	; Anzahl der Words zum schreiben
	move.w	target,D0
	bsr	seldri
	bsr	minidelay	; ??? Wieso ist das noetig?
	bsr	do_erase
	bsr	WaitFlag
	moveq	#0,D3
	bra	StatOut
do_erase
	movem.l	D0-D1/A0,-(SP)	; checken ob mfm mit $AA gefuellt ist
	move.l	mfm3,A0
	move.l	#$AAAAAAAA,D1
	moveq	#12,D0
1$	cmp.l	(A0),D1
	bne	fill	; NEIN
	adda.w	#$0400,A0
	dbf	D0,1$
	bra.s	mfm_ok
	;
fill	move.w	#$3800/4,D0	; mfm3 $1C02 Words $AA fuellen
	move.l	mfm3,A0
1$	move.l	D1,(A0)+
	dbf	D0,1$	
mfm_ok	movem.l	(SP)+,D0-D1/A0
	move.w	#$7FFF,$9E(A6)
	move.w	#$9100,$9E(A6)	; MFM und Fast BIT setzen
	move.l	mfm3,$20(A6)	; Diskpointer, mfm3 enthaelt $AAAA
startdma
	move.w	#$3002,$9C(A6)	; Clear Request
	move.b	#1,flag
	move.w	disklen,$24(A6)
	move.w	disklen,$24(A6)
	rts
;----------------------------------------------------------------------------
sortbuffer:
	bsr	mfmcopy
	bne	rtrn
	move.l	ptr,A0
	addq.l	#4,A0
	bra	CheckTrack

mfmcopy	move.l	ptr,A1
	move.l	#$AAAAAAAA,D3
	move.l	#$44894489,D4
1$	cmp.w	(A1)+,D4
	beq.s	1$
	suba.w	#10,A1
	move.l	D3,(A1)
	move.l	D4,4(A1)
	move.l	12(A1),D1
	move.l	8(A1),D0
	asl.l	#1,D0
	and.l	D3,D0
	andi.l	#$55555555,D1
	or.l	D1,D0
	andi.l	#$000000ff,D0	;Sectors til gap
	cmp.w	#11,D0		;Sector 0 steht am Anfang des Puffers !!
	bgt	tofewsc
	bne	copymem
	move.l	A1,ptr
	bra	sortok

copymem	move.l	D0,D1
	mulu	#$440,D1	; Sectors * mfmlength
	move.l	A1,A0
	adda.l	D1,A0	; Pointer to gap
	move.w	#$0400,D1
sloop	cmp.w	(A0)+,D4	; find first sector after gap
	dbeq	D1,sloop
	tst.w	D1
	bmi	no2sync	; keinen 2.ten Sync gefunden

	cmp.w	(A0),D4
	beq	twosync
	subq.l	#2,A0
twosync	subq.l	#6,A0
	move.l	D3,(A0)
	move.l	D4,4(A0)
	move.l	A0,A2	; pointer to first sector after gap
	move.l	A0,ptr	; = Zeiger auf Trackstart

	moveq	#11,D1
	sub.l	 D0,D1	; D0 = Sectors before gap
	mulu	#$440,D1	; Sectoren nach Gap
	adda.l	D1,A2	; Destination for MEMCopy
	move.l	(A2),(A1)	; Clockbits kopieren!!
	mulu	#$440,D0	; Anzahl Bytes zum kopieren
	move.l	A2,D1
	add.l	 D0,D1	; A2+D0 = Ende des Tracks
	sub.l	 A0,D1	; minus Anfang des Tracks
	cmp.w	#$2EC0,D1
	bne	 tofewsc
	lsr.l	#2,D0	; Anzahl Bytes/4 =Longs
	addq.l	#4,D0	; 2 extra Longs fuer Clockbits in GAP
1$	move.l	(A1)+,(A2)+
	dbf	D0,1$
sortok	moveq	#0,D0
	rts


CheckTrack:
	move.l	A0,A1	; wird von CHECKDISK gerufen
	bsr	SetRegs	; D3,D4,D5,D7
findsync
	cmp.w	(A1)+,D4
	bne	nosync
	cmp.w	(A1),D4
	bne.s	1$
	addq.l	#2,A1
1$	bsr	CheckHeader	; D6 = DataCheckSum
	moveq	#0,D0
	move.w	TMP,D0
	bne	rtrn
	move.w	#$00FF,D1
	lea	56(A1),A1
2$	move.l	(A1)+,D0
	and.l	D3,D0
	eor.l	D0,D6
	dbf	D1,2$
	tst.l	D6
	bne	blcksum
	dbf	D5,nextblock
	moveq	#0,d0
	rts

nextblock
	cmp.w	(A1),D4
	beq	findsync
	adda.w	#2,A1
	move.l	A1,D0
	sub.l	A0,D0
	cmpi.w	#$32c0,D0	; READLEN ($1960) * 2 
	bgt	tofewsc
	bra	nextblock


SetRegs	clr.w	TMP
	moveq	#0,D7		; Headerlong erstellen (oberes WORD)
	move.w	track,D7	; Tracknummer errechnen
	add.w	D7,D7
	move.w	head,D1
	eor.w	#1,D1
	add.w	D1,D7
	or.w	#$FF00,D7	; Amigaformat
	move.l	#$55555555,D3
	move.l	#$44894489,D4
	moveq	#$0A,D5		; 11 Sektoren suchen
	rts

CheckHeader:
	move.l	A1,-(SP)	; Header Checksum nach D0
	moveq	#40,D1		; 40 Bytes
	bsr	checksum
	move.l	(SP)+,A1
	move.l	D0,D2
	move.l	40(A1),D0	; Header Checksum dekodieren
 	move.l	44(A1),D1
	and.l	D3,D0			
	lsl.l	#1,D0
	and.l	D3,D1
	or.l	D1,D0	
	cmp.l	D0,D2
	beq	1$
	bsr	hecksum
	move.w	D0,TMP

1$	move.l	(A1),D0		; Header holen und ueberpruefen
	move.l	4(A1),D1
	and.l	D3,D0			
	lsl.l	#1,D0
	and.l	D3,D1
	or.l	D1,D0		; Header = $FF TRACK SECNUM SEC_TIL_END	
	move.l	D0,D2
	swap	D2
	cmp.w	D2,D7		; D7 = $FF+TRACKNR.
	beq	2$
	bsr	headerr
	move.w	D0,TMP
2$	swap	D2		; D2 = HeaderLONG	

	move.l	48(A1),D0	; BlockChecksum dekodieren
	move.l	52(A1),D1
	and.l	D3,D0
	lsl.l	#1,D0
	and.l	D3,D1
	or.l	D1,D0
	move.l	D0,D6		; D6 = DataBlockCheckSum
	rts

;-----------------------------------------------------------------------------
; Wandelt MFM-Track in Datenblock um 
; Parameter :
;	A0 = Pointer auf Datenblock
;	A1 = Pointer auf MFM-Puffer
;	ptr muss gueltig sein!!

DecodeTrack:
	movem.l	D1-D7/A2,-(a7)				
	bsr	SetRegs	; D3,D5,D7

findsyncx
	move.w	#$440/2,D0
1$	cmp.w	(A1)+,D4
	dbeq	D0,1$
	beq	syncfound	
	bsr	nosync
	move.w	D0,TMP		; NO SYNC FOUND
	bra	enddecode

syncfound
	cmp.w	(A1),D4		; 2.ter SYNC ?
	bne.s	1$
	addq.l	#2,A1
1$	bsr	CheckHeader	; Header in D2, datablockchecksum in D6
	and.l	#$0000FF00,D2
	lsl.l	#1,D2
	and.l	#$0000FF00,D2	;war vorher and.l #$00000FF00,D2
	cmp.l	#$1400,D2	
	bls.s	goon2
	bsr	tofewsc		; Problem: wohin kopieren?
	move.w	D0,TMP
	bra	nextblockx
	;
goon2	move.l	A0,A2		; Pointer in Destination Puffer errechen
	add.l	D2,A2
	moveq	#$7F,D2		; Daten decodieren und Checksum berechnen
	lea	56(A1),A1
decolop	move.l	512(A1),D0
	and.l	D3,D0
	eor.l	D0,D6
	move.l	(A1)+,D1
	and.l	D3,D1
	eor.l	D1,D6
	asl.l	#1,D1
	or.l	D1,D0
	move.l	D0,(A2)+	; LONG speichern
	dbf	D2,decolop
	tst.l	D6
	beq.s	1$
	bsr	blcksum
	move.w	D0,TMP
1$	dbf	D5,nextblockx

enddecode
	moveq	#0,D0
	move.w	TMP,D0
	movem.l	(A7)+,D1-D7/A2
	rts
nextblockx
	move.l	A1,D0	; Ende
	sub.l	ptr,D0	; -Start
1$	cmp.w	(A1),D4
	beq	findsyncx
	addq.l	#2,A1
	addq.l	#2,D0
	cmp.l	#$32C0,D0	; READLEN ($1960) * 2 
	blt	nextblockx
	bsr	tofewsc
	move.w	D0,TMP
	bra	enddecode

;-----------------------------------------------------------------------------
; Wandelt Datenblock ($1600 Bytes) in MfM-Track um inklusiv Header + Checksum
; Parameter :
;	track
;	head
;	a0 = Pointer auf Datenblock
;	a1 = Pointer auf MFM-Puffer

CodeTrack:
	movem.l	A0-A3/D0-D7,-(A7)
	move.l	A1,A3	; MFMPUFFER
	move.l	#$AAAAAAAA,-4(A1)
	moveq	#0,D5	; aktuelle SecNR	
	moveq	#$0B,D6	; secs till end
k3	move.l	A3,A1
	moveq	#0,D0	; LONG NULL kodieren
	bsr	codelong
	move.l	#$44894489,4(A3)

	moveq	#0,D0	; Headerlong erstellen
	move.w	track,D0	; Tracknummer errechnen
	lsl.w	#1,D0
	move.w	head,D1
	eor.w	#1,D1
	add.w	D1,D0
	or.w	#$FF00,D0	; Amigaformat
	asl.l	#8,D0
	move.b	D5,D0	; SecNum
	asl.l	#8,D0
	move.b	D6,D0	; Secs till end
	lea	8(A3),A1	; Header in Puffer schreiben
	bsr.s	codelong

	moveq	#3,D4	; 16 Nullen kodieren, ab 16(A3)
1$	moveq	#0,D0
	bsr.s	codelong
	dbf	D4,1$

	lea	8(A3),A1	; Checksum ueber Header
	moveq	#40,D1	; = 40 Bytes berechnen
	bsr	checksum
	lea	48(A3),A1
	bsr.s	codelong	; D0 kodieren ab A1
	
	lea	64(A3),A2
	moveq	#$7F,D4
k2	move.l	A2,A1
	move.l	(A0)+,D0
	move.l	D0,D3
	lsr.l	#1,D0
	bsr.s	clockbits
	move.l	D3,D0
	lea	512-4(A1),A1
	bsr.s	clockbits
	addq.l	#4,A2
	dbf	D4,k2

	lea	64(A3),A1
	bsr.s	grenz
	lea	64+512(A3),A1
	bsr.s	grenz

	lea	64(A3),A1
	move.w	#$400,D1
	bsr.s	checksum
	lea	56(A3),A1
	bsr.s	codelong	; Pruefsumme in D0 kodieren
		
	adda.l	#$0440,A3
	addq.w	#1,D5		; aktuelle Sektornr
	subq.w	#1,D6
	bne	k3
	move.l	A3,A1
	moveq	#0,D0
	bsr.s	codelong
	movem.l	(A7)+,A0-A3/D0-D7
	rts

codelong:
	move.l	D0,D3
	lsr.l	#1,D0
	bsr.s	clockbits
	move.l	D3,D0
	bsr.s	clockbits
grenz	move.b	(A1),D0	; Grenzkorrektur
	btst.b	#0,-1(A1)
	bne.s	1$
	btst.l	#6,D0
	bne.s	3$	; fertig
	bset	#7,D0
	bra.s	2$		
1$	bclr	#7,D0
2$	move.b	D0,(A1)
3$	rts

clockbits:
	andi.l	#$55555555,D0
	move.l	D0,D2
	eori.l	#$55555555,D2
	move.l	D2,D1
	lsl.l	#1,D2
	lsr.l	#1,D1
	bset.l	#31,D1
	and.l	D2,D1
	or.l	D1,D0
	btst	#0,-1(A1)
	beq.s	1$
	bclr.l	#31,D0
1$	move.l	D0,(A1)+
	rts

checksum:
	move.l	D2,-(SP)
	lsr.w	#2,D1
	subq.w	#1,D1
	moveq	#0,D0
1$	move.l	(A1)+,D2
	eor.l	D2,D0
	dbf	D1,1$
	andi.l	#$55555555,D0
	move.l	(SP)+,D2
	rts
	; Tabelle der Fehlermeldungen

tofewsc	moveq	#1,D0	; to few Sectors
	rts
nosync	moveq	#2,D0	; No sync found
	rts
no2sync	moveq	#3,D0	; no second sync found
	rts
hecksum	moveq	#4,D0	; Header Checksum Error
	rts
headerr	moveq	#5,D0	; Error in Header/Format Long
	rts
blcksum	moveq	#6,D0	; Block Checksum Error
	rts

;-----------------------------------------------------------------------------
;
;	Routinen fuer RAM-COPY
;
InsertSource
	movem.l	D2-D7/A0-A6,-(A7)
	lea	4$(pc),A0
	bra	txout
 IFD GER
4$	DC.B	"Quelldisk oder Zieldisk einlegen !",0
 ELSE
4$	DC.B	"Insert source- or destination disk!",0
 ENDC
	EVEN

InsertTarget
	movem.l	D2-D7/A0-A6,-(A7)
	lea	txin(pc),A0
txout	moveq	#GREEN,D0
	bsr	Status
	TON_OK
	
txout1	bsr	RamHandler
	cmp.w	#2,D0	; 0=Start, 1=Continue, 2=Stop
	beq	UserBreak
	move.l	D0,-(A7)
	bsr	StatusClear
	move.l	(A7)+,D0
	movem.l	(A7)+,D2-D7/A0-A6
	rts
 IFD GER
txin	DC.B	"Zieldiskette(n) einlegen !",$00
 ELSE
txin	DC.B	"Insert destination disk(s)!",0
 ENDC
	EVEN

nodiskin
	movem.l	D2-D7/A0-A6,-(A7)
	bsr	Bit2Num
	move.l	D0,D1	; in D1 Drive!!
	bsr	NoDiskIN
	TON_BAD
	bra	txout1

tprotec	movem.l	D2-D7/A0-A6,-(A7)
	bsr	Bit2Num
	move.l	D0,D1
	bsr	WritePROT
	TON_BAD
	bra	txout1
;-----------------------------------------------------------------------------

RamPos	move.l	D0,D1
	bsr	Bit2Num		; D0 = DriveBit 1,2,4,8, danach 0,1,2,3
	lea	DriStat,A0
	tst.b	(A0,D0.w)	; Drive war schon mal selektiert
	bne.s	1$	
	move.b	#1,(A0,D0.w)
	bsr	track0		; beim ersten Aufruf auf Track 0 fahren
	movem.l	D3-D4,-(A7)
	moveq	#0,D4		; Vergleichstrack = 0
	bra	dop
1$	and.w	D5,D1
	beq	rtrn		; keine Positionierung erforderlich

Go2S_Track
	movem.l	D3-D4,-(SP)
	move.w	track,D4	
dop	move.w	s_track,D3	; Kopf befindet sich auf D4, nach GO2S_Track
	cmp.w	D4,D3		; auf S_TRACK
	beq.s	setok
	bgt.s	to80
to00	sub.w	D3,D4
	subq.w	#1,D4
pos0	bsr	dir00
	bsr	stepdelay
	dbf	D4,pos0
	bra.s	setok
to80	sub.w	D4,D3
	move.w	D3,D4
	subq.w	#1,D4
pos8	bsr	dir80
	bsr	stepdelay
	dbf	D4,pos8
setok	movem.l	(SP)+,D3-D4
	bra	bigdelay


;	***-----------------------------------------------------
;	***
;	*** RAM COPY
;	***
;	***-----------------------------------------------------
Driver:	tst.w	s_track		; nur bei kopie ab track 00 viruschecken
	bne.s	.novchk
	tst.w	s_head
	bne.s	.novchk
	;
	clr.w	s_head		; bei QFormat immer Positionierung auf track0
	clr.w	s_track
	move.l	#$1600/8,D0	; mfm2 mit Daten fuellen und nach mfm1 codieren
	bsr	fillmfm2
	;
	move.w	source,d0
	bsr	drive
	bsr	track0		; auf Track 0 positionieren
	;
	clr.w	track
	move.w	track,D4	; Starttrack fahren
	beq.s	.stayps
.nxtstp	bsr	stepall		; Stept die Drives einzeln
	bsr	stepdelay	; Delay fuer alle
	subq.l	#1,d4
	bne.s	.nxtstp

.stayps	bsr	bigdelay	; stabilisieren aller Laufwerke
	clr.w	err1
	clr.w	err2
	;
	bsr	VirusCheck
	;
.novchk	clr.l	DriStat
	clr.w	track
	IFNE SOLO
	move.l	MemPTH,LowMem	; SINGLE, untere Speichergrenze merken
	ENDC
	;
driloop	move.w	s_head,-(A7)
1$	move.w	source,D0
	move.w	D0,all
	bsr	drive		; hochfahren
	beq.s	4$	
	move.w	source,D0
	bsr	nodiskin
	bra	1$		; neu anwaehlen
	;
4$	move.w	source,D0
	bsr	RamPos		; auf Track 0 positionieren
	move.w	s_track,track
	move.l	#rloop,RamVec	; Leseschleife
	bsr	RamRW
	bsr	DrivesOFF
	bsr	InsertTarget
	move.w	source,D5	; nur SourceDrive neu positionieren 

ins_tar	moveq	#3,D3		; alle TargetDrives positionieren
	moveq	#1,D4
tarlop	moveq	#0,D0
	move.w	target,D0
	move.w	D0,all
	and.l	D4,D0
	beq.s	notsel		; Drive nicht selected
	bsr	drive
	beq.s	1$
	move.l	D4,D0
	bsr	nodiskin
	bra	tarlop
	;
1$	move.l	D4,D0		; BitNR.
	bsr	RamPos		; auf s_track positionieren
	btst	#3,$BFE001	; Destination write protected ??
	bne.s	notsel
	move.l	D4,D0
	bsr	tprotec
	bra	tarlop
	;
notsel	asl.l	#1,D4
	dbf	D3,tarlop

	move.w	(SP),s_head
	move.w	s_track,track
	move.w	s_track,D0
	bsr	StatClear
	move.l	#wloop,RamVec
	bsr	RamRW
	bsr	DrivesOFF	; alle Koepfe stehen auf track,head !
	bsr	InsertSource
	move.w	target,D5	; alle Drives positionieren!
	cmp.w	#1,D0		; Write again ?
	beq	ins_tar

	add.w	#2,SP		; Head vom Stapel holen
	bsr	ClrMEMTAB	; belegten Speicher freigegben
	
	tst.w	FLG		; Alles kopiert?
	beq	Complete	; JA
	move.w	track,s_track	; neuer Start
	move.w	head,s_head
	bra	driloop


RamRW	move.w	#1,FLG		; Indikator fuer Ruecksprungursache
	move.w	all,D0
	bsr	seldri
	move.w	side,D0
	beq.s	bothRW
	cmp.w	#1,D0
	beq	uppRW

lowRW	clr.w	s_head
	bsr	LowerR
	bne	ilret
	bra	ramstep

uppRW	bsr	UpperR
	bne	ilret
	bra	ramstep

bothRW	tst.w	s_head		; auf ersten Cylinder nur side1
	bne	lowRW		; kopieren
	move.w	track,D0
	cmp.w	endtrack,D0
	blt.s	1$
	tst.w	endhead		; auf letzten Cylinder nur side0
	beq	uppRW		; kopieren
1$	bsr	UpperR
	bne	ilret
	bsr	LowerR
	bne	ilret

ramstep	clr.w	FLG		; STATUS= fertig setzen
	move.w	track,D0
	addq.w	#1,D0
	cmp.w	endtrack,D0
	bgt	ilret
	move.w	D0,track
	bsr	stepall		; Stept alle Laufwerke einzeln
	bsr	stepdelay	; Delay aber fuer alle
	bra	RamRW

LowerR	move.w	all,D0	; ** eingefuegt am 11.12.90
	bsr	seldri	; **
	move.w	#1,head
	bsr	bamtest
	bne	okret
	bsr	side1
	move.l	RamVec,A0
	jmp	(A0)

UpperR	move.w	all,D0	; ** eingefuegt am 11.12.90
	bsr	seldri	; **
	clr.w	head
	bsr	bamtest
	bne	okret
	bsr	side0
	move.l	RamVec,A0
	jmp	(A0)

; ** Ram-Copy Leseroutine

rloop:	cmp.w	#NIBBLE,mode
	beq	NibR
	move.l	#$1600,D0	; Bytesize
	bsr	GetMem		; freien Speicher holen
	bne	ilret		; keinen mehr gefunden
	move.w	#3,tries
2$	move.l	mfm1,ptr
	move.l	ptr,A0
	bsr	FastRead
	bsr	WaitFlag
	beq.s	1$
	clr.l	(A0)
1$
	bsr	GetPtr		; Memorypuffer nach A0 holen
	move.l	(A0),A0
	move.l	ptr,A1
	bsr	DecodeTrack	; decodieren und eventuelle Fehler korrigieren
	tst.w	D0
	beq.s	mesout
	subq.w	#1,tries
	bne.s	2$
	;
mesout	move.l	D0,D3
	move.l	d0,.errnum
	bsr	StatOut
	tst.l	D3
	beq	okret
	cmp.w	#BAMCOPY,mode
	beq	okret
	cmp.w	#DOSPLUS,mode
	beq	okret		; REPAIR hat stattgefunden !!!
	;
	bsr	GetPtr		; bei DosCopy wird ReadError genibbelt!
	move.l	(A0),A1		; daher Speicher freigeben
	clr.l	(A0)+	
	moveq	#0,D0	
	move.w	(A0),D0		; Speicherblockgroesse ($1600)
	clr.w	(A0)+
	IFEQ SOLO
				; wenn Multitasking, Speicher dynamisch
	bsr	FreeMem		; freigeben
	ENDC

	IFNE SOLO
	sub.l	D0,MemPTH	; SINGLE, nur obere Speichergrenze aendern
	ENDC
	bsr	AskReadError
	bsr	NibR
	sne	d7
	move.l	.errnum,d0
	move.l	d0,d3
	bsr	StatOut
	tst.w	d7
	rts
	;
.errnum	DC.L 0

; ** Ram-Copy Schreibroutine

wloop	bsr	GetPtr		; SpeicherPTH holen
	tst.l	(A0)
	beq	ilret		; Ende
	cmp.w	#$1600,4(A0)	; Test auf NibbleMode
	bne	NibW
	bsr	code_write	; kodiert Track nach mfm1 und schreibt ihn	
 
	moveq	#0,D3		; zunaechst mal OK fuer alle Drives ausgeben
	moveq	#YELLOW,D4
	bsr	Display

	moveq	#3,D2		; 4 Drives testen
	moveq	#1,D1

rtest	movem.l	D1-D2,-(SP)	; auf VERIFY testen
	move.l	D1,D0
	and.b	Verify,D0
	beq	rnext
	move.b	D0,actdriv	; muss gesetzt werden fuer Retry
	bsr	seldri		; Drive in D0 selektieren
	move.w	#3,tries	; fuer jedes Drive 3 Versuche

rveri	move.l	mfm1,A0	
	move.l	A0,ptr
	bsr	FastRead	; Puffer einlesen
	bsr	WaitFlag
	bne	wri_again	; No Sync
	move.l	mfm2,A0		; als VERIFY gelesenen MFMPuffer nach 
	move.l	ptr,A1		; MFM2 (genau $1600 Bytes) dekodieren
	bsr	DecodeTrack
	tst.l	D0
	bne	wri_again	; ERROR
	moveq	#0,D3		; Drive OK
	moveq	#YELLOW,D4
	bsr	Display
rnext	movem.l	(SP)+,D1-D2
	lsl.l	#1,D1	
	dbf	D2,rtest
	bra	okret

wri_again:
	subq.w	#1,tries
	bne.s	1$		; nach 3 Versuchen RETRY/CANCEL
	bsr	RetryCancel	; setzt ev. tries neu, Drive in actdri
	bne	rnext		; Abbruch, Verify auf verbleibenden Drives
1$	moveq	#8,D3		; VERIFY ERROR ausgeben
	bsr	StatOut		; DRIVE ist unklar
	bsr.s	code_write
	bra	rveri

code_write:
	bsr	GetPtr
	move.l	(A0),A0
	move.l	mfm1,A1
	move.l	A1,ver3		; fuer Verify
	move.l	A1,ptr
	bsr	CodeTrack	; automatisches DELAY
	move.l	mfm1,A0		; alten MFMPuffer nochmal schreiben
	move.l	A0,ptr
	bsr	FastWrite
	bra	WaitFlag

NibR:	bsr	NibbleRead	; source muss selektiert sein
	bsr	Analyse		; setzt D1, A3, readlen, offset
	movem.l	D1/A3,-(SP)
	moveq	#16,D0		; 4 Bytes fuer Variable
	add.w	readlen,D0
	add.w	offset,D0
	bsr	GetMem		; Speicher holen
	movem.l	(SP)+,A3/D0	
	tst.l	(A0)
	beq	ilret
	movem.l	A0/A3,-(SP)
	bsr	NibMesOut
	movem.l	(SP)+,A0/A3
	move.l	(A0),A0		; Pointer auf Speicherblock
	move.w	readlen,(A0)+	; Variable speichern
	move.w	offset,(A0)+
	;
CopyMFM:
	moveq	#0,D0
	add.w	readlen,D0
	add.w	offset,D0
	lsr.w	#2,D0		; Laenge/4+1
	addq.w	#1,D0
1$	move.l	(A3)+,(A0)+
	dbf	D0,1$
	bra.s	okret
	;
NibW	bsr	minidelay	; Wieso ???
	move.w	#$DC00,disklen
	bsr	do_erase
	bsr	GetPtr
	move.l	(A0),A3		; Source
	move.l	mfm1,A0		; Destination
	move.w	(A3)+,readlen
	move.w	(A3)+,offset
	bsr	CopyMFM
	bsr	NibbleWrite	; setzt A3 auf mfm1 / WaitFlag
	moveq	#YELLOW,D0
	bsr	NibMesOut
;	bra	okret

okret	ori.b	#4,CCR		; Z-Flag setzen
	rts
ilret 	andi.b	#%11111011,CCR	; Z-Flag loeschen
	rts
;-------------------------------------------------------------
;	
;	Memory Management fuer RAM-COPY und OPTIMIZE
;

GetMem:	bsr.s	GetPtr
	tst.l	(A0)
	bne	UserBreak	; es ist schon Speicher reserviert
	movem.l	D0/A0,-(A7)
	moveq	#0,D1	; in D0 ByteSize
	bsr	AllocMem
	move.l	D0,D1
	movem.l	(A7)+,D0/A0
	tst.l	D1
	beq	ilret
	move.l	D1,(A0)	; Pointer auf Speicherblock
	move.w	D0,4(A0)	; Size eintragen

;	movem.l	D0/A0-A1,-(SP)
;	move.l	#(160*6)/4,D0
;	move.l	MemTab,A0
;	move.l	#$7e800,A1
;1$	move.l	(A0)+,(A1)+
;	dbf	D0,1$
;	movem.l	(SP)+,D0/A0-A1

	bra	okret

; ** Errechnet aus track und head einen Eintrag in MemTab
; ** In MemTab stehen die Pointer auf schon reservierte Speicherbloecke, 
; ** zum Zwecke der Trackpufferung.

GetPtr	move.l	D1,-(A7)
	move.w	track,D1
	add.w	D1,D1
	add.w	head,D1
	mulu	#6,D1
	move.l	MemTab,A0
	add.l	D1,A0	
	move.l	(SP)+,D1
	rts

; ** ClrMEMTAB wird nach jeder mode-Aktion angesprungen, um ev.
; ** reservierten Speicher freizugeben.
 
ClrMEMTAB
	move.l	MemTab,A0
	move.w	#163,D1	; max. 164 Eintraege => 6*164=984 Bytes
clr0	move.l	(A0),D0	; kein Eintrag enthalten
	beq.s	1$
	movem.l	D1/A0,-(A7)
	move.l	D0,A1
	moveq	#0,D0
	move.w	4(A0),D0	; Size
 IFEQ SOLO
	bsr	FreeMem
 ELSE
	move.l	LowMem,MemPTH	; SINGLE, untere Speichergrenze setzen
 ENDC
	movem.l	(A7)+,D1/A0
1$	clr.l	(A0)+
	clr.w	(A0)+
	dbf	D1,clr0
	rts
;---------------------------------------------------------------
;                 mfm1       mfm2     mfm3
;
;   DOSCOPY      $6C00/4    $6c00/4  $6C00/4  (default)   
;   BAMCOPY      default 
;   DOSPLUS      default
;   NIBBLE       default
; R DOSCOPY      $6C00/4    $3400    $1C10   
; R BAMCOPY      DOSPLUS
; R DOSPLUS      $3800/4    $1600    0
; R NIBBLE       $6800      0        $1C10

;   OPTIMIZE     $3800/4    0        $2100 (TimeTab usw.) 
;   FORMAT       defauft
;   QFORMAT      default
;   ERASE        default
;   MESLEN       default

;   NAME         default
;   DIR          default
;   CHECK        default
  
;	*** MFMAlloc
;	*** --------
MFMAlloc:
	bsr	RamMode
	lea	mfm1,A0		; mfm1,mfm2,mfm3 muessen nacheinander liegen
	moveq	#2,D2
1$	moveq	#0,D3
	move.w	(A1)+,D3	; Size aus Tabelle holen
	beq.s	2$
	movem.l	D2-D3/A0-A1,-(SP)
	move.l	#$10003,D1	; CHIP	
	move.l	D3,D0
	bclr	#0,D0
	bsr	AllocMem	
	movem.l	(SP)+,D2-D3/A0-A1
	tst.l	D0
	beq	NoMemory
	btst	#0,D3
	beq.s	2$
	add.l	#$0400,D0
2$	move.l	D0,(A0)+
	dbf	D2,1$
	rts

;	*** MFMFree
;	*** -------
MFMFree:
	bsr	RamMode
	lea	mfm1,A0
	moveq	#2,D2
2$	tst.l	(A0)
	beq.s	1$
	moveq	#0,D0
	move.w	(A1)+,D0
	beq.s	1$
	movem.l	D2/A0-A1,-(SP)
 	move.l	(A0),A1
	btst	#0,D0
	beq.s	3$
	sub.l	#$0400,A1
	bclr	#0,D0
3$	bsr	FreeMem			; FreeMem
	movem.l	(SP)+,D2/A0-A1
1$	clr.l	(A0)+
	dbf	D2,2$
	rts

RamMode	clr.w	TMP
	move.w	mode,D0
	tst.w	device
	bne.s	co_ram
s_def	lea	mdef(PC),A1
	cmp.w	#OPTIMIZE,D0
	bne.s	1$
	lea	mopt(PC),A1
1$	rts
	;
co_ram	lea	1$(PC),A1
	cmp.w	#DOSCOPY,D0
	beq.s	4$
	lea	2$(PC),A1
	cmp.w	#DOSPLUS,D0
	beq.s	4$
	cmp.w	#BAMCOPY,D0	; BAMCOPY identisch mit DOSPLUS
	beq.s	4$
	lea	3$(PC),A1	; NIB
	cmp.w	#NIBBLE,D0
	bne.s	s_def
4$	move.w	#1,TMP
	rts

; ** wenn Bit 1 gesetzt, #$400 addieren!!
1$	DC.W	$6c01,$3400,$6C10	; alt: $3810 statt $6C10
2$	DC.W	$3801,$1600,$0000	 ; alt: $3400 statt $1600
3$	DC.W	$6c01,$0000,$6C10	; alt: $3810 Puffer 3 nur fuer mestrack!!
mopt	DC.W	$3801,$0000,$2100
mdef	DC.W	$6C01,$6C01,$6C01

;---------------------------------------------------------------------------
;
;	Routinen zur Steuerung der Laufwerke	  REGS: D0-D1
;
drive:	move.l	D0,-(SP)
	asl.b	#3,D0
	eor.b	#$FF,D0
	move.b	#$7F,$bfd100
	nop
	nop
	moveq	#$7F,D1
	and.b	D0,D1
	move.b	D1,$bfd100
	bsr	bigdelay
	bsr	bigdelay
	StartTimer 917000	; us
.tstrdy	btst	#5,$BFE001	; 20 Takte
	beq.s	.dskrdy		; 10 Takte
	TestTimer .tstrdy
	moveq	#1,D1
	bra.s	.dskerr
	;
.dskrdy	moveq	#0,D1
.dskerr	move.l	(SP)+,D0
	tst.l	D1		; FLAGS setzen!!
	rts

side0:	bclr	#2,$bfd100
	bra	udly
side1:	bset	#2,$bfd100
	bra	udly

seldri	tst.w	D0		; veraendert nur das Drive Bit
	beq	rtrn
	asl.b	#3,D0
	eor.b	#$FF,D0
	ori.b	#$78,$BFD100    ; latch drives
	nop
	nop
	and.b	D0,$BFD100
	rts

BreakStep
	addq.w	#1,track
	move.w	track,D0
	cmp.w	endtrack,D0
	bgt	Complete
	bsr.s	stepall		; Laufwerke einzeln steppen
	bra	stepdelay	; Delay fuer alle Laufwerke
	;
stepall	move.l	D2,-(A7)
	moveq	#3,D2
	moveq	#1,D1
steplp	move.w	all,D0
	and.l	D1,D0
	beq.s	stepnx
	bsr	seldri
	bsr	udly
	bsr	dir80
	bsr	minidelay
stepnx	lsl.w	#1,D1
	dbf	D2,steplp
	move.l	(A7)+,D2
	rts

;	*** DrivesOFF
;	*** ---------
DrivesOFF:
	ori.b	#$78,$bfd100
	ori.b	#$80,$bfd100
	nop
	nop
	and.b	#$87,$bfd100
	bsr	stepdelay
	ori.b	#$f8,$bfd100
	rts

;	*** track0
;	*** ------
track0:	move.w	#83,TMP
tra_lop	subq.w	#1,TMP
	beq	rtrn
2$	move.b	$bfe001,D0
	btst	#4,D0		; track 0
	beq.s	1$
	bsr	dir00
	bsr	stepdelay
	bra	tra_lop
1$	bsr	bigdelay
	move.b	$bfe001,D0
	btst	#4,D0		; nicht Track0 
	bne	2$
	rts

dir00:	bset	#1,$bfd100
	bra.s	step
	;
dir80:	bclr	#1,$bfd100
step	nop
	nop
	bclr	#0,$bfd100
	nop
	nop
	nop
	bset	#0,$bfd100
	nop
	rts
;---------------------------------------------------------
;
; Zeitschleifen fuer diverse Zwecke
;
bigdelay
	move.l	D0,-(A7)
	move.l	#34048,D0	; us
	bra.s	loopy
	;
stepdelay
	move.l	D0,-(A7)
	move.l	#6100,D0	; us
	bra.s	loopy
	;
minidelay
	move.l	D0,-(A7)
	move.l	#1100,D0	; us
	bra.s	loopy
	;
udly:	move.l	D0,-(A7)
	move.l	#263,D0	; us
	;
loopy	bsr	x_Delay	; Delay in us in D0
	move.l	(A7)+,D0
	rts

;diskready	rts
;	btst	#5,$bfe001
;	bne	diskready
;	rts


;
;  XCOPY NIBBLE MODULE
;	
;  Date      Who        Changes                                           
;  --------- ---------- --------------------------------------------------
;  18-MAR-89 Frank      komplette Ueberarbeitung
 	
	
TRACKLEN	EQU $30C0	; Default-Laenge in MFM-Bytes
RLEN		EQU $3400	; d.h maximale Tracklen

coutab	EQU $40			; Offsets in die Tabelle WorkTab
syncpos	EQU coutab+$80

	BSSSEG

readlen		DS.W	1
offset		DS.W	1
middle		DS.L	1
TrackEnd	DS.L	1
tmp		DS.W	1	

	CODESEG

;	*** SpeedCheck
;	*** ----------
 IFD GER
mestxt	DC.B	"Tracklaenge: $%x = %d Bytes (MFM)",0
 ELSE
mestxt	DC.B	"Tracklength: $%x = %d bytes (MFM)",0
 ENDC
	EVEN

SpeedCheck:
	move.w	target,D0
	move.w	D0,tmp		; aktuelles Drive
	bsr	seldri
	move.w	#$DC00,disklen	; aktuellen Track mit $AAAA loeschen
	bsr	do_erase	
	bsr	WaitFlag	; auf Loeschen fertig warten

	clr.l	drilen		; erzwingt neues Ausmessen des Tracks
	clr.l	drilen+4
	move.l	mfm3,A3
	bsr	Read2UM		; 2 Umdrehungen einlesen, A3 konsistent
	bne	SpecialERR	; Fehler aufgetreten
	bsr	getracklen	; D0 enthaelt Track-Laenge in Bytes
	move.w	D0,-(SP)	; einmal Hex und einmal Dezimal ausgeben
	move.w	D0,-(SP)
	XPRINTF buf,mestxt,4	; 4 Bytes sind Stackkorrektur fuer 2 mal D0 
	lea	buf,A0
	moveq	#GREEN,D0
	bsr	Status
	moveq	#0,D3		; gruene Null ausgeben
	bsr	StatOut
	bra	SpecialERR	


;	*** NibbleTrack
;	*** -----------
NibbleTrack:
	move.w	source,D0
	bsr	seldri
	bsr	NibbleRead
	;
	move.w	target,D0
	bsr	seldri	; seldri hat internen DELAY!
	move.w	#$DC00,disklen
	bsr	do_erase	
	bsr	Analyse		; A3 zeigt danach auf Pufferanfang
nib_go	move.l	D1,-(SP)	; disklen ist noch nicht gesetzt!!
	bsr	WaitFlag	; auf Loeschen fertig warten
	moveq	#3,D3		; alle TargetDrives einzeln waehlen
	moveq	#1,D4
nib_lop	move.w	target,D0
	and.w	D4,D0
	beq.s	nex_dri		; Drive nicht selected
	movem.w	D3-D4,-(SP)	
	move.w	D0,tmp
	bsr	seldri
	bsr	mestrack	; Schreiblaenge nach disklen, fuer jedes
	bsr	nib_write	; Drive individuell
	movem.w	(SP)+,D3-D4
nex_dri	asl.w	#1,D4
	dbf	D3,nib_lop
	move.l	(SP)+,D0
	rts

NibbleWrite 	
	move.l	mfm1,A3
	moveq	#YELLOW,D1
	bra	nib_go

NibbleRead
	move.w	source,tmp
	move.l	mfm1,A3		; ca. 2 Umdrehungen einlesen
	;
Read2UM	move.l	A3,-(SP)
	move.w	#RLEN!$8000,disklen
	add.l	#$6000,A3
	move.w	#(RLEN*2-$6000)/4,D0 ; die letzten $800 Bytes loeschen	
1$	clr.l	(A3)+		; Block in Bytes
	dbf	D0,1$
	move.l	(SP)+,A3
	;
nib_write
	move.w	#$7FFF,$9E(A6)	; ADKCON clear
	move.w	#$9100,$9E(A6)	; Taktrate des Disk-Controllers auf MFM	
	move.w	#$3002,$9C(A6)	; clear Request (INDEX+DSKBLK)
	move.l	A3,$20(A6)	; Diskpointer
	move.b	#1,flag
	move.b	$BFDD00,D0	; ICR lesen, d.h. REQUESTS loeschen
	move.b	#$90,$BFDD00	; Index-IRQ ON => DMA-Start mit naechstem Index
	or.b	#$10,I_enable	; Bit auch in Sytem-Maske setzen
	StartTimer 917500	; Maximalwert (muss fuer 2 UM reichen!!)
	;
wdma	tst.b	flag		; flg wird entweder druch DSKBLK (R/W) oder  
	beq.s	ok_nibRW	; durch INDEX geloescht (R, wenn 2 UM)
	TestTimer wdma
	move.b	#$10,$BFDD00	; INDEX Bit in ICR loeschen
	and.b	#$EF,I_enable	; Bit auch in Sytem-Maske loeschen
	;
	move.w	disklen,$24(A6)
	move.w	disklen,$24(A6)
;	move.w	#$0F00,$180(A6)	; Bildschirm rot
	move.w	tmp,D0
	bsr	Bit2Num
	movem.l	D0-D7/A0-A6,-(SP)
	move.l	D0,D1
	bsr	NoIndexIN	; Meldung ausgeben
	movem.l	(SP)+,D0-D7/A0-A6
	bsr	WaitFlag	; auf DSKBLK DONE warten
;	clr.w	$180(A6)
	moveq	#1,D0		; Errror
	rts
ok_nibRW
	move.b	#$10,$BFDD00	; INDEX Bit in ICR loeschen
	and.b	#$EF,I_enable	; Bit auch in Sytem-Maske loeschen
	moveq	#0,D0
	rts

;-----------------------------------------------------------------
;
; ** errechnen der max. Schreiblaenge, ev. Target ausmessen
; ** InputRegs : D3 -> aktuelles Target-Drive 
; ** OutputRegs: liefert in disklen die Schreiblaenge zurueck
	BSSSEG
drilen	DS.W	4		; 4 Eintraege fuer 4 Drives 
	CODESEG

mestrack:
	move.w	D3,D0	; Track ist vorher mit $AAAA!!
	eori.w	#3,D0		; beschrieben worden
	add.w	D0,D0		; mal 2
	lea	drilen,A0
	adda.w	D0,A0
	move.w	(A0),D2
	bne.s	notempty	; DRIVE ist schon einmal ausgemessen
	;
	movem.l	A3/A0,-(A7)
	move.l	mfm3,A3	; Diskpuffer
	bsr	Read2UM
	move.l	#TRACKLEN+$20,D2 ; Default Laenge
	tst.w	D0
	bne.s	setlen		; Fehler aufgetreten
	bsr	getracklen	; Track-Laenge ausmessen und nach D0
	move.w	D0,D2

setlen	clr.l	(A3)		; mfm3 ungueltig, um es neu fuellen
	movem.l	(A7)+,A3/A0
	sub.w	#$20,D2		; 32 Bytes fuer Laufwerkschwankungen abziehen
	move.w	D2,(A0)		; Laenge speichern
	;
notempty
	cmp.w	readlen,D2	; MIN (SourceLEN,TargetLEN) suchen
	blt.s	min		; target < source
	move.w	readlen,D2	; readlen = NULL => spezieller Schutz erkannt
min	add.w	offset,D2	; wenn OFFSET, dann addieren
	;
	lsr.w	#1,D2		; D2/2 = Tracklaenge in WORDS
	or.w	#$C000,D2
	move.w	D2,disklen
	rts
;--------------------------------------------------------------------
; ** Nachdem RLEN*2 Bytes oder 2UM eines Tracks eingelesen worden  
; ** sind, stellt getracklen die Laenge eines Tracks in Bytes fest
; ** InputRegs : A3 -> Zeiger auf MfmBuffer 
; ** OutputRegs: D0 -> Track-Laenge in Bytes 
; **             A1 -> Zeigt auf Lese-Ende im MfmPuffer  

getracklen:
	move.w	#RLEN-2,D0	; Read LEN in Bytes!!
	lea	RLEN*2(A3),A1	; Ende des Puffers
1$	tst.w	-(A1)		; letztes Word im Puffer suchen
	dbne	D0,1$
	addq.l	#2,A1
	move.l	A1,D0
	sub.l	A3,D0		; 2*Tracklaenge in Bytes
	lsr.w	#1,D0		; durch 2 => Mitte, D0 = Tracklaenge
	and.w	#$FFFE,D0	; gerade, wegen LEA!
	rts

;--------------------------------------------------------------------
;
;	Untersucht MFM-Code ab A3, A3 zeigt danach auf Schreibstart	
;	readlen -> gelesene Laenge des Tracks in MFM-Bytes
;	offset  -> Anzahl der MFM-Bytes, um in GAP auszukommen

Analyse	bsr	getracklen
	move.l	A1,TrackEnd
	move.w	D0,readlen	; Tracklaenge in Bytes speichern,
	lea	(A3,D0.W),A1	; entspricht Pufferlaenge in WORDS
	move.l	A1,middle	; Ende des 1.ten Tracks

Search	move.l	A3,A2		; Anfang des Puffers = Trackanfang
	move.w	sync,D1
	cmp.w	#INDEXCOPY,D1
	beq.s	stdsync
	move.w	#$9521,D2
	move.w	#$A245,D3
	move.w	#$A89A,D4
	move.w	#$448A,D5
	move.w	#$4489,D6
	;
	move.w	(A2)+,D0	; Trackpuffer auf SYNCS untersuchen
nextword
	swap	D0
	move.w	(A2)+,D0
	swap	D0
	moveq	#$0F,D7
1$	cmp.w	D0,D1
	beq.s	syncfind
	cmp.w	D0,D2
	beq.s	syncfind
	cmp.w	D0,D3
	beq.s	syncfind
	cmp.w	D0,D4
	beq.s	syncfind
	cmp.w	D0,D5
	beq.s	syncfind
	cmp.w	D0,D6
	beq.s	syncfind
	rol.l	#1,D0
	dbf	D7,1$
	cmp.l	A1,A2		; Pufferende erreicht?
	blt	nextword	; nein
	bra	NOSYNCS

stdsync	move.w	#$4489,D1
	move.w	(A2)+,D0	; Trackpuffer auf SYNCS untersuchen
2$	swap	D0
	move.w	(A2)+,D0
	swap	D0
	moveq	#$0F,D7
1$	cmp.w	D0,D1
	beq.s	syncfind
	rol.l	#1,D0
	dbf	D7,1$
	cmp.l	A1,A2		; Pufferende erreicht?
	blt	2$		; nein
	bra	NOSYNCS

syncfind:
	move.w	D0,-(SP)
	move.w	D0,D1
	move.l	WorkTab,A0
	lea	syncpos(a0),A0
	moveq	#24,D6		; Platz fuer maximal 24 Sektoren
store	move.l	A2,(A0)+	; Pointer auf Sync speichern
	add.l	#$100,A2	; mininale Sektorlaenge ($100 MFM-Bytes)
	move.w	(A2)+,D3
	subq.w	#1,D6
	beq.s	getlast		; Sektortabelle voll
	;
nextsy2	swap	D3
	move.w	(A2)+,D3
	swap	D3
	moveq	#$0F,D7
	bra.s	2$
1$	rol.l	#1,D3
2$	cmp.w	D3,D1
	dbeq	D7,1$
	beq	store
	rol.l	#1,D3
	cmp.l	A1,A2		; 1.ten Track ueberschritten
	blt	nextsy2
	;
getlast	move.l	TrackEnd,A1	; absolutes Pufferende
	move.l	WorkTab,A2
	lea	syncpos(a2),A2
	move.l	(A2),A2		; Pointer auf ersten Sync
	sub.l	A3,A2		; MINUS Pufferanfang => Offset
	add.l	middle,A2	; PLUS Anfang des 2.ten Tracks
	subq.l	#8,A2
	move.w	(A2)+,D3
	;
nextsy3	swap	D3		; 1.ten Sync auf 2.tem Track finden
	move.w	(A2)+,D3
	swap	D3
	moveq	#$0f,D7
	bra.s	2$
	;
1$	rol.l	#1,D3
2$	cmp.w	D3,D1
	dbeq	D7,1$
	beq.s	store1		; Pointer auf letztes SYNC speichern
	rol.l	#1,D3
	cmp.l	A1,A2
	blt	nextsy3
store1	move.l	A2,(A0)+	; letzten SYNC in Tabelle eintragen
	clr.l	(A0)		; ENDE-MARKER in syncpos
;-------------------------------------------------------------
;
;	gefundene SYNCS und deren Position analysieren
;
	move.l	WorkTab,A2	
	lea	syncpos(A2),A0	; Sektorlaengen errechnen
	moveq	#0,D3		; Anzahl der Sektoren NULL
1$	move.l	4(A0),D2
	beq.s	sync_end
	sub.l	(A0)+,D2
	move.w	D2,(A2)+	; Sektorlaenge speichern
	addq.w	#1,D3		; Anzahl der Sektoren +1
	bra	1$

sync_end
	clr.l	(A2)		; ENDE_MARKER in lentab
	move.l	WorkTab,A2	
	lea	coutab(A2),A2
	clr.l	(A2)		; Hilfstabelle COUNT zum zaehlen
	moveq	#1,D4		; D4 zeigt spaeter auf 1.ten SYNC nach GAP
	cmp.w	#1,D3		; nur ein grosser Sektor ?
	beq	oneblock	; JA

	move.l	WorkTab,A0	; gleichlange Sektoren zaehlen
nex_len	move.w	(A0)+,D1	; Sektorlaenge aus Tabelle holen
	beq.s	gapsearch	; Ende, GAP suchen
	;
	move.l	WorkTab,A2	
	lea	coutab-4(A2),A2
	;
nex_cmp	addq.l	#4,A2
	move.w	(A2),D2	
	beq.s	new_entry	; neue Laenge ermittelt
	sub.w	D1,D2
	bpl.s	1$
	neg.w	D2
1$	cmp.w	#$20,D2		; Toleranz +/- 32 Bytes
	bgt	nex_cmp
	move.w	D1,(A2)		; Laenge speichern
	addq.w	#1,2(A2)	; Vorkommen +1
	bra	nex_len
	;
new_entry
	move.w	D1,(A2)		; Laenge
	clr.w	2(A2)		; Vorkommen = 0
	clr.l	4(A2)		; ENDE-MARKER
	bra	nex_len

gapsearch
	move.l	WorkTab,A0	
	lea	coutab(A0),A0	; die Laenge,die am wenigsten vorkommt
	moveq	#$7f,D2		; wird als GAP betrachtet. D2=MAX
1$	move.l	(A0)+,D1	; Minimum suchen, i.d.R  0 oder 1
	beq.s	gap_fou		; TABEND
	cmp.w	D2,D1
	bgt.s	1$		; letzten kleinen nehmen!!
	move.w	D1,D2
	swap	D1
	move.w	D1,D5		; Laenge des Minimums nach D5
	bra.s	1$
	;
gap_fou	move.l	WorkTab,A0	; Laenge suchen
	moveq	#0,D4		; GAP Sektornr. nach D4
1$	addq	#1,D4
	cmp.w	(A0)+,D5
	bne	1$
;-------------------------------------------------------------
;
;	 Schreiblaenge und Schreibstart ermitteln
;
oneblock:
	move.w	D4,D2		; D4 zeigt auf SYNC nach GAP
	cmp.w	D3,D4
	blt.s	1$
	moveq	#0,D2		; GAP am Ende oder nur ein Sektor
1$	lsl.w	#2,D2		; NR. des Sektors mal 4
	move.l	WorkTab,A0
	lea	syncpos(A0),A0
	move.l	(A0,D2.W),A1	; Start der Daten des Tracks
	lea	-10(a1),a1	; sub.l #10,a1
;	sub.l	#$0A,A1		; Differenz zu Pufferanfang ermitteln
	cmp.l	A3,A1		; Start in Naehe von Pufferanfang?
	bge.s	dostest		; NEIN
	;
	move.w	D4,D2		; Schreibanfang hochsetzen (2.ter Track)
	lsl.w	#2,D2
	move.l	(A0,D2.W),A1
;	move.l	middle,A1	; auf 2.ten INDEX-2 positionieren
	lea	-10(a1),a1	;sub.l #10,a1
;	sub.l	#$0A,A1		; Schreibstart hochsetzen
	;
dostest:
;	move.l	A1,A3		; GEFAHR: verschiebt sich relativ zum INDEX
	move.w	(SP)+,D2	; SYNC restaurieren
	cmp.w	#$4489,D2	; Test auf DosTrack
	bne.s	notdos
	cmp.w	#$000B,D3	; 11 Sektoren
	bne.s	notdos
	move.l	WorkTab,A2
	lea	coutab(A2),A2	; A2 scratch
1$	move.l	(A2)+,D1	; D5 scratch
	beq.s	notdos
	cmp.l	#$04400009,D1	; 11 * $440 Sektoren
	bne	1$
	moveq	#GREEN,D1
	bra.s	getback		; PSEUDO-DOSTRACK = GREEN
	;
notdos	cmp.w	#INDEXCOPY,sync	; INDEXCOPY?
	bne.s	blue_ok		; nein
	move.l	middle,A1	; auf 2.ten INDEX-2 positionieren
	subq.l	#2,A1
	move.l	A1,A3		; Schreibstart hochsetzen
	;
blue_ok	moveq	#BLUE,D1	; Nibbletrack = BLUE
	bra.s	getback
	;
grey_ok	moveq	#LGREY,D1	; Bruchstellen-Schutz
	move.w	#'BR',D2	; hat keine Funktion nur fuer REPORT gedacht
	bra.s	getback
	;
red_ok	moveq	#RED,D1		; nichts gefunden = RED/ NO SYNC
	move.w	#'NS',D2	; hat keine Funktion nur fuer REPORT gedacht
getback	move.l	A1,D5
	sub.l	A3,D5		; Pufferende-Anfang = Offset	
	move.w	D5,offset
	;
	cmp.w	#$3300,readlen	; auf ueberlangen Track testen
	blt.s	1$		; kleiner
	moveq	#0,D1		; LONG TRACK!! D1=Sonderfall
1$	rts			; Rueckmeldung in D1

NOSYNCS	bsr.s	Neuhaus		; Bruchstellen testen, da kein SYNC
	beq	grey_ok		; Bruchstelle gefunden
	move.l	middle,A1	; A1 = 2.ter Track
	subq.l	#2,A1		; auf 2.ten Index-2 positionieren
	move.l	A1,A3		; Schreibstart hochsetzen
	cmp.w	#INDEXCOPY,sync	; IndexCopy?	
	bne	red_ok		; nein
	bra	blue_ok		; nichts gefunden, ROTE 2 ausgeben

;synctab
;	DC.W	$9521,$A245,$A89A,$448A,$4489,$0000,$0000,$0000,$0000
;	DC.W	$9521 		; ARKANOID SYNC
;	DC.W	$A245 		; BEYOND THE ICE PALACE
;	DC.W	$A89A		; MERCENERY/BACKLASH

Neuhaus	movem.l	D2-D7,-(SP)
	moveq	#0,D0
	move.w	readlen,D0
	move.l	D0,D7	; TrackLen nach D7
	subq.w	#8,D7
	moveq	#0,D6	; Bruchstellenzaehler
	move.l	A3,A0	; A3=Puffer Anfang
	move.l	A3,A2
	;
vergleich
	move.b	(A0)+,D1
comp	cmp.b	(A0)+,D1
	bne.s	ungleich
	subq.w	#1,D7
	bne	comp
	lea	(A3,D0.W),A1	; A1=Pufferende
	move.b	(A2),D1		; Wert VOR letzter Bruchstelle
2$	move.b	D1,(A2)+	; bis Trackende schreiben
	cmp.l	A1,A2	
	ble	2$
	move.l	A3,A0		; Trackanfang schreiben
	move.b	(A0),D1
	move.w	#$300,D2
	sub.l	#$300,A3	; A3=Pufferstart korrigieren
1$	move.b	D1,-(A0)
	dbf	D2,1$
	;
	movem.l	(SP)+,D2-D7
	move.l	A1,D0		; Ende
	sub.l	A3,D0		; Ende - Anfang = Laenge in Bytes
	move.w	D0,offset	; wichtig fuer RAM-COPY
	clr.w	readlen		; -> special protection
	moveq	#0,D1
	rts
	;
ungleich
	addq.w	#1,D6
	move.l	A0,A2		; Pointer auf letzte Bruchstelle
	subq.l	#8,A2		; merken
	cmp.w	#5,D6
	ble	vergleich
	movem.l	(SP)+,D2-D7	; zu viele Bruchstellen
	moveq	#1,D1
	rts


;
;  XCOPY OPTIMIZE MODULE
;	
;  Date      Who        Changes                                           
;  --------- ---------- --------------------------------------------------
;  02-APR-89 Frank      Erstellung
;  05-APR-89 Frank      Letzte Aenderung	
;  26-DEC-89 Frank      Aenderung der Blockallokation beim Optimize
   

b_type	equ 0
b_key	equ 4
b_seq	equ 8
b_size	equ 12
b_next	equ 16
b_first	equ 16
b_check	equ 20
b_hash	equ 496
b_parent	equ 500
b_extens	equ 504
b_subtype	equ 508

T_SHORT	equ 2	; subtype -> USERDIR,FILE,ROOT
T_DATA	equ 8	; no subtype
T_LIST	equ 16	; subtype -> FILE

ST_USERDIR	equ 2
ST_FILE	equ -3
ST_ROOT	equ 1

	BSSSEG

real_pos	DS.W	1
secnum		DS.W	1
lock		DS.W	1
lock1		DS.W	1
oldblk		DS.L	1
HKey		DS.L	1
FileHead	DS.L	1
dblock		DS.L	1
parent		DS.L	1
nextfree	DS.L	1
BITMAP		DS.L	1
new_pth		DS.L	1

TimeTab		DS.L	1
BlockTab	DS.L	1
DirTab		DS.L	1
DirHig		DS.L	1
DirLow		DS.L	1
FileTab		DS.L	1
FileHig		DS.L	1
FileLow		DS.L	1

block_write_flag DS.W	1

	CODESEG

InitReadBlock:
	movem.l	D0/D1/A0,-(SP)
	move.l	mfm3,D0		; mfm3 muss auf einen Puffer von
	move.l	D0,A0		; mindestens $2000 Bytes Groesse zeigen
	move.w	#$2000/4-1,D1	; 8KB fuer Puffer loeschen
1$	clr.l	(A0)+
	dbf	D1,1$
	move.l	D0,TimeTab
	add.l	#160*2,D0	; + 320 Bytes
	move.l	D0,BlockTab	; +3520 Bytes 
	move.l	D0,A0		; =3840
	moveq	#0,D1
2$	move.w	D1,(A0)+	; Blocknr. initialisieren
	addq.w	#1,D1		; (1760 * 2 = 3520 Bytes) 
	cmp.w	#1760,D1
	blt.s	2$
	add.l	#1760*2,D0
	move.l	D0,DirTab	; Platz fuer 160 Directory Eintraege
	move.l	D0,DirHig	; (160 Blocknummern a 2 Bytes)
	move.l	D0,DirLow
	add.l	#160*2,D0	; +320
	move.l	D0,FileTab	; 8KB-4160 Eintraege fuer Files und Extens
	move.l	D0,FileHig
	move.l	D0,FileLow
	movem.l	(SP)+,D0/D1/A0
	st	block_write_flag ; sperren = $FF, d.h bei ReadBlock wird kein
	rts			; Puffer zurueckgeschrieben

Optimize:
	moveq	#0,D1			; Type: ALL
	bsr	AvailMem		; Division durch Null!
	cmp.l	#22*$1600,D0		; 22 Tracks
	blt	NoMemory		; mindestens 123904 Bytes Puffer

	bsr	InitReadBlock
	sf	block_write_flag	; Puffer zurueckschreiben erlauben (= $00)

	move.w	target,D0
	bsr	seldri
	clr.l	BITMAP
	
	moveq	#10-1,D7		; Pufferzahl
	move.l	#902-10*11,D6		; Tracks 35-40 einlesen = 10 Puffer
5$	move.l	D6,D0	
	bsr	ReadBlock
	add.l	#11,D6
	dbf	D7,5$
	move.l	#880,D0	; RootBlock
	move.l	D0,nextfree
;--------------------------------------------------------------------------
;
;	move_dir:
;	es werden alle UserDir-,FileHeader- und Extensbloecke verschoben,
;	sowie b_key, b_hash, b_parent, b_extens und die Hash-Tabelle angepasst		
;
move_dir
	move.l	D0,parent	; parent ist die neue BlockNR.!
	bsr	ReadBlock	; Directoryblock einlesen
	move.l	A0,A3	; A3 momentaner DIRECTORY BLOCK
	bsr	LockTrack
	cmp.l	#880,nextfree	; bei 1.tem Aufruf Bitmap creieren
	bne.s	ScanDir
	lea	$13C(A3),A0	; Pointer auf BITMAP BlockNR. holen
	bsr	MoveBlock	; BITMAP nach 881 schieben
	move.l	dblock,A0	; Pointer auf BitMap-Block
	move.l	A0,BITMAP
	moveq	#-1,D1
	moveq	#512/4-1,D0	; BitMap loeschen
1$	move.l	D1,(A0)+
	dbf	D0,1$
	move.l	#880,D0		; Block 880 und 881 belegen
	bsr	AllocBlock
	move.l	#881,D0
	bsr	AllocBlock

ScanDir	lea	$18(A3),A2	; Hash-Tabelle eines Directories durchlaufen
	moveq	#71,D7		; maximal 72 Eintraege
get_hash
	tst.l	(A2)
	beq	nex_hash
	move.l	A2,A0
move_chain
	bsr	MoveBlock	; D3-D7/A2-A5 konsistent
	move.l	nextfree,D1	; aktuelle BlockNR.
	move.l	dblock,A1	; Pointer auf Blockadr.
	cmp.l	#T_SHORT,b_type(A1)
	bne	BadType
	move.l	HKey,D0	; Stimmt alte BlockNR. mit HeaderKey ueberein
	cmp.l	b_key(A1),D0
	bne	BadKey
	move.l	D1,b_key(A1)	; Header-Key korrigieren
	move.l	parent,b_parent(A1)

	move.l	b_subtype(A1),D0	
	cmp.l	#ST_USERDIR,D0
	bne.s	1$
	move.l	DirHig,A0	; Directory-Liste aufbauen, USERBLOCKNR.
	move.w	D1,(A0)+	; neue BlockNR. speichern
	move.l	A0,DirHig
	bra	tst_hash

1$	cmp.l	#ST_FILE,D0
	bne	BadType
	move.l	D1,FileHead	; FileHeader BlockNR.  als PARENT setzen
	bsr	Lock1		; Track sperren
	cmp.l	#1,b_seq(A1)	; nur ein DATA-Block?
	bne.s	2$
	tst.l	b_extens(A1)
	bne	 BadExtens
	movem.l	D7/A1-A3,-(SP)
	bsr	GetNextFree	
	move.l	D0,b_first(A1)	; First-Data korrigieren
	moveq	#1,D6		; SequenceNR.
	lea	$134(A1),A0
	bsr	MoveDataBLK
	bne	BadSeq
	movem.l	(SP)+,D7/A1-A3
	bra.s	tst_hash

2$ 	move.l	FileHig,A0	; File-Liste aufbauen, FILEBLOCKNR.
	move.w	D1,(A0)+	; neue BlockNR. speichern	
	move.l	A0,FileHig

	tst.l	b_extens(A1)	; bei Files ev. EXTENS-Block verschieben
	beq.s	tst_hash	; kein EXTENS-Block vorhanden
	movem.l	D7/A1-A3,-(SP)
lmove_ext
	lea	b_extens(A1),A0
	bsr	MoveBlock	; EXTENS-Block verschieben
	move.l	dblock,A1	; Position holen
	cmp.l	#T_LIST,b_type(A1)
	bne	BadType
	cmp.l	#ST_FILE,b_subtype(A1)
	bne	BadType
	move.l	nextfree,D1
	move.l	D1,b_key(A1)	; Header-Key korrigieren
	move.l	FileHead,b_parent(A1)
 	move.l	FileHig,A0	; File-Liste aufbauen, EXTENSBLOCKNR.
	move.w	D1,(A0)+	; neue BlockNR. speichern	
	move.l	A0,FileHig
	tst.l	b_extens(A1)
	bne	lmove_ext
	movem.l	(SP)+,D7/A1-A3

tst_hash
	clr.w	lock1
	move.l	b_hash(A1),D0	; hat Eintrag aus Tabelle Nachfolger?
	beq.s	nex_hash	; NEIN, naechsten Eintrag aus Tabelle holen
	lea	b_hash(A1),A0	; HashChain durchlaufen
	bra	move_chain

nex_hash
	addq.l	#4,A2
	dbf	D7,get_hash
	move.l	DirLow,A0
	move.w	(A0)+,D0
	beq.s	move_files
	ext.l	D0
	move.l	A0,DirLow
	bra	move_dir
;--------------------------------------------------------------------------
;
;	move_files:
;	es werden alle Datenbloecke verschoben, sowie
;	b_key, b_first, b_data, und b_next angepasst		
;
move_files:
	move.l	FileLow,A0
	move.w	(A0)+,D0	; D0 ist neue BlockNR.
	beq	Flush		; Ende, alle Puffer auf Disk schreiben
	move.l	A0,FileLow
	ext.l	D0
	move.l	D0,-(SP) 
	bsr	ReadBlock	; Fileheader- oder Extensblock einlesen
	move.l	A0,A3		; A3 momentaner FILEHEADER/EXTENS BLOCK
	bsr	LockTrack	; Track vor Freigeben sperren
	move.l	(SP)+,D0	; BlockNR.
	;
	cmp.l	#T_SHORT,b_type(A3)
	bne.s	1$
	move.l	D0,FileHead	; neuer FileHeader ist PARENT fuer DATA-Bloecke
	bsr	GetNextFree	
	move.l	D0,b_first(A3)	; First-Data korrigieren
	moveq	#1,D6		; Zaehler fuer Reihenfolge der Daten
	bra.s	data_lop
1$	cmp.l	#T_LIST,b_type(A3)
	bne	BadType
	
data_lop
	move.l	b_seq(A3),D7	; Pointer auf Datenblocks
	subq.l	#1,D7
	lea	$134(A3),A2	; Start der BlockNR.
move_data
	tst.l	(A2)
	beq.s	2$
	move.l	A2,A0		; Datenblock verschieben
	bsr	MoveDataBLK
	beq.s	2$		; Fileende, da letzter DatenBlock ( D7=0 )
	;
	bsr	GetNextFree
	move.l	D0,b_next(A1)	; Next-Block korrigieren
	addq.l	#1,D6		; Sequence + 1
	subq.l	#4,A2		
	dbf	D7,move_data
2$	cmp.w	#1,D7		; Kette ist abgebrochen
	bge	BadSeq
	bra	move_files

MoveDataBLK
	bsr	MoveBlock
	move.l	dblock,A1	; Position holen
	move.l	nextfree,D1
	cmp.l	#T_DATA,b_type(A1)
	bne	BadType
	move.l	FileHead,b_key(A1) ; Header-Key korrigieren
	cmp.l	b_seq(A1),D6
	bne	BadSeq
	tst.l	b_next(A1)
	rts
;------------------------------------------------------
;
;	Hilfsroutinen und Fehlermeldungen
;

AllocBlock
	tst.l	BITMAP	; Block in BitMap als belegt kennzeichnen
	beq	rtrn
	movem.l	D1/A0,-(SP)
	move.l	BITMAP,A0
	subq.l	#2,D0
	move.l	D0,D1
	lsr.l	#5,D1	; Byte-Offset= D1/32
	lsl.l	#2,D1	; D2*4
	addq.l	#4,D1	; Checksum ueberspringen
	add.l	D1,A0
	and.l	#$1F,D0	; BitNR. isolieren
	move.l	(A0),D1
	bclr	D0,D1
	move.l	D1,(A0)
	movem.l	(SP)+,D1/A0
	rts

;GetNextFree
;	move.l	nextfree,D0	: alte Blockbelegungsroutine, XCOPY 2.0
;	cmp.l	#880,D0
;	bge 1$
;	subq.l	#1,D0
;	cmp.l	#2,D0
;	blt	DiskFull
;	rts
;1$	addq.l	#1,D0
;	cmp.l	#1760,D0
;	blt	rtrn
;	move.l	#879,D0
;return	rts	


GetNextFree:
	move.l	nextfree,D0
	cmp.w	#880,D0
	blt.s	z_runter
	cmp.w	#901,D0
	bgt.s	z_hoch
	addq.l	#1,D0	; Sonderbehandlung 880-901 = Track 40
	cmp.w	#902,D0
	blt	rtrn
	move.w	#879,D0	; weitermachen mit Block 879 
	rts
	;
z_runter
	subq.l	#1,D0	; zwischen 2 und 879
	cmp.w	#2,D0	; Block 0 und 1 = Bootblock
	bge	rtrn
	move.w	#902,D0	; weitermachen mit Block 902 auf Track 41
	rts
	;
z_hoch	addq.l	#1,D0
	cmp.w	#1760,D0
	bge.s	DiskFull
	rts
	
LockTrack
	move.l	D0,-(SP)	; zuletzt gelesenen Track sperren
	move.w	track,D0
	add.w	D0,D0
	add.w	head,D0
	move.w	D0,lock
	move.l	(SP)+,D0
	rts

Lock1	move.l	D0,-(SP)	; zuletzt gelesenen Track sperren
	move.w	track,D0
	add.w	D0,D0
	add.w	head,D0
	move.w	D0,lock1
	move.l	(SP)+,D0
	rts

BadType	moveq	#1,D0
	bra.s	GoOptError
BadSeq	moveq	#2,D0
	bra.s	GoOptError
BadKey	moveq	#3,D0
	bra.s	GoOptError
Double	moveq	#4,D0
	bra.s	GoOptError
BadExtens
	moveq	#5,D0
	bra.s	GoOptError
DiskFull
	moveq	#6,D0
	bra.s	GoOptError
Logic	moveq	#9,D0
;	bra.s	GoOptError
GoOptError
	bra	OptError
;------------------------------------------------------
;
;	MOVE alten Block zu optimaler Position
;

MoveBlock:
	bsr	GetNextFree
	move.l	D0,nextfree	
	move.l	(A0),D0		; alte logische Sourceblocknr.
	cmp.l	#2,D0
	blt	BadKey
	cmp.l	#1760,D0
	bge	BadKey
	move.l	D0,HKey	; als Header-Key merken 
	move.l	nextfree,(A0)	; neue Blocknummer eintragen

	add.l	D0,D0	; alte BlockNR. umrechnen
	move.l	BlockTab,A0
	add.l	D0,A0
	move.w	(A0),D0	; auf physikalische Blocknr.
	bmi	Double
	move.w	#$FFFF,(A0)
	ext.l	D0
	move.w	D0,oldblk 
	bsr	ReadBlock
	move.l	A0,-(SP)	; Pointer auf Sektor retten

	move.l	nextfree,D0	; Destination Blocknr.
	bsr	AllocBlock	; in BitMap belegen
	move.l	nextfree,D0	; Destination Blocknr.
	bsr	ReadBlock	; ReadBlock darf den 1.ten Block nicht
	move.l	A0,dblock	; wieder auslagern!!
	move.l	(SP)+,A1	; A1 = Source, A0 = Destination
	moveq	#512/4-1,D0	; Speicherbloecke tauschen	
1$	move.l	(A0),D1
	move.l	(A1),(A0)+	; move SOURCE 2 DESTINATION
	move.l	D1,(A1)+
	dbf	D0,1$
	
	move.l	nextfree,D0	; logische Destinationblocknr.
	add.l	D0,D0
	move.l	BlockTab,A1
	add.l	D0,A1		; physikalische Blocknummern tauschen
	tst.w	(A1)
	bpl.s	2$		; Nextfree war nicht Source Block
	move.l	BlockTab,A0	
	move.l	nextfree,D1
	move.w	#1759,D0
3$	cmp.w	(A0)+,D1
	dbeq	D0,3$
	bne	rtrn		; nicht gefunden
	move.w	oldblk,-(A0)
	rts
	;
2$	move.w	oldblk,(A1)	; neue Position des Blocks merken
	rts
;---------------------------------------
;   
;  parms  D0.w	-> Blocknr. 0 - 1759
;  return A0   -> Zeiger auf Sektor
;
;  INFO:   Track  Block  Cyl  Head
;            0     0-10   0    0
;            1    11-21   0    1
;            2    22-32   1    0
;            3    33-43   1    1      usw.

ReadBlock:
	movem.l	D1-D7/A1-A3/A5,-(SP)
	move.w	track,real_pos
	ext.l	D0
	divu	#22,D0
	move.w	D0,s_track
	move.w	#1,s_head
	swap	D0
	cmp.w	#11,D0
	blt.s	1$
	sub.w	#11,D0
	clr.w	s_head

1$	move.w	D0,secnum
	move.w	s_track,track	; wichtig fuer GetPtr
	move.w	s_head,head
	bsr	GetPtr

	tst.l	(A0)
	bne	track_in	; Track steht schon im Speicher
	move.l	#$1600,D0	; Speicher holen
	bsr	GetMem
	beq.s	2$		; noch freien Speicher gefunden

	move.l	A0,new_pth	; kein Speicher mehr, track freigeben
	tst.l	(A0)
	bne	Logic
	move.w	s_track,-(SP)
	bsr	FreeTrack	; einen TrackSpeicher freigeben
	move.w	(SP)+,s_track

2$	move.w	real_pos,track	; track = s_track
	bsr	Go2S_Track
	move.w	s_track,real_pos
	move.w	s_track,track
	move.w	s_head,head
	tst.w	head
	bne.s	3$
	bsr	side0
	bra.s	4$
3$	bsr	side1
4$	bsr	ReadDecode

track_in
	move.l	TimeTab,A0
	move.w	track,D0
	add.w	D0,D0
	add.w	head,D0
	add.w	D0,D0

	move.w	SYSTime+2,(A0,D0.W)	; nur unteres WORD von SYSTIME

	bsr	GetPtr			; Pointer auf Sektor errechnen

	move.l	(A0),A0
	move.w	secnum,D0
	mulu	#$200,D0
	add.l	D0,A0
	move.w	real_pos,track
	movem.l	(SP)+,D1-D7/A1-A3/A5
	rts

;	*** ReadDecode
;	*** ----------
ReadDecode:
	move.w	#3,tries
rlop	move.l	mfm1,ptr
	move.l	ptr,A0
	bsr	FastRead
	bsr	WaitFlag
	beq.s	1$
	clr.l	(A0)
1$	bsr	GetPtr	; Memorypuffer nach A0 holen
	tst.l	(A0)
	beq	Logic
	move.l	(A0),A0
	move.l	ptr,A1
	bsr	DecodeTrack	; track,head,ptr !!
	tst.l	D0
	beq	mesout1
	subq.w	#1,tries
	bne	rlop
	move.l	D0,d3	; ** 11.12.90: 3 Leseversuche waren erfolglos
	bsr	StatOut	; ** => Vorgang abbrechen 
	bra	DosError	; kein DosTrack !!

mesout1	move.l	D0,D3
	bra	StatOut
;--------------------------------------------------------------
;
;	gibt den Trackpuffer frei der am laengsten nicht
;	benutzt wurde, d.h. das Minimum aus TimeTab

FreeTrack:
	move.l	TimeTab,A0
	add.l	#160*2,a0 	; Tabellen Ende
	moveq	#-1,D1
	move.w	#159,D0		; 160 Eintraege
1$	move.w	-(A0),D2
	beq.s	2$
	cmp.w	D2,D1
	bls.s	2$
	cmp.w	#81,D0		; Track 81 = RootTrack
	beq.s	2$
	cmp.w	lock,D0		; gesperrter Track
	beq.s	2$
	cmp.w	lock1,D0
	beq.s	2$
	move.w	D2,D1		; neues Minimum
	move.w	D0,D3		; Minimumnr merken
	move.l	A0,A1		; Pointer in Timetab merken
2$	dbf	D0,1$
	tst.w	D1
	bmi	Logic
	move.w	D3,D0		; D0 = NR
	movem.l	D0/A1,-(SP)	; A1 = Pointer in Timetab
	tst.b	block_write_flag
	bne.s	no_twrite
	bsr	WriteTR		; A0 = Zeiger in MemTab auf alten Eintrag 
	bra.s	set_pths

no_twrite
	mulu	#6,D0		; A0 selbst errechnen
	move.l	MemTab,A0
	add.l	D0,A0
	tst.l	(A0)
	beq	Logic
 
set_pths
	move.l	new_pth,A1
	move.l	(A0),(A1)	; Pointer auf Speicherblock und Size
	move.w	4(A0),4(A1)	; eintragen
	clr.l	(A0)		; alten Eintrag loeschen
	clr.w	4(A0)
	movem.l	(SP)+,D0/A1
	clr.w	(A1)		; Track nicht mehr im Speicher  
	rts			; TimeTab (loeschen)


;	*** WriteTR
;	*** d0: track
;	*** ---------
WriteTR:
	move.w	D0,-(SP)
	clr.w	head
	bsr	side0
	move.w	(SP),d0
	lsr.w	#1,D0
	bcc.s	3$
	move.w	#1,head
	bsr	side1
3$	move.w	real_pos,track
	move.w	D0,s_track
	bsr	Go2S_Track
	move.w	s_track,real_pos
	move.w	s_track,track
	move.w	(SP)+,D0	; 0 <= D0 <= 159
	mulu	#6,D0
	move.l	MemTab,A0
	add.l	D0,A0
	tst.l	(A0)
	beq	Logic
	move.l	A0,-(SP)	; Zeiger in MemTab retten
	move.l	(A0),A0		; Zeiger auf Track
	move.l	A0,-(SP)
	moveq	#10,D2	; fuer 11 Sektoren Pruefsummen berechnen
sum_lop	cmp.l	#T_SHORT,(A0)
	beq.s	calcsum
	cmp.l	#T_DATA,(A0)
	beq.s	calcsum
	cmp.l	#T_LIST,(A0)
	bne.s	nocalc
	;
calcsum	lea	b_check(A0),A1
	bsr	BlockSum
nocalc	lea	$200(a0),a0	; nächster Sektor
	dbf	D2,sum_lop
	move.l	(SP)+,A0	; Track schreiben, Zeiger auf Track
	move.l	mfm1,A1
	move.l	A1,ptr
	bsr	CodeTrack
	move.l	ptr,A0
	bsr	FastWrite
	bsr	WaitFlag
	moveq	#0,D3
	moveq	#YELLOW,D4
	bsr	Display
	move.l	(SP)+,A0	; Zeiger in MemTab holen
	rts

;---------------------------------------------------------------------------
; ** folgende Grafik veranschaulicht, welche Bereiche jeweils bei einem
; ** bestimmten Wert von nextfree(^) zurueckgeschrieben(.) werden
;
;  Tracks 0/1          80/81        158/159 
;          |-------------|-------------|
;                ^........                   Fall 1:  0<= nextfree <=81
;          .....................^            Fall 2:  nextfree>81
 
; Fehlerbehebung fuer Flush Funktion am 17.2.91 eingehackt
;

 
Flush:	move.l	BITMAP,A0
	move.l	A0,A1
	bsr	BlockSum	; Checksum fuer BITMAP
	;
	move.l	nextfree,D0	; letzte benutzte Blocknr.
	divu	#11,D0		; in Tracknr umrechnen

	moveq	#0,d0
	moveq	#0,d1
	moveq	#0,d2
	cmp.w	#80,d0
	bge.s	highdos
	move.w	d0,d1
	move.w	#81,d2
	bra.s	f_lop
	;
highdos	move.w	#80,d1
	cmp.w	#81,d0
	bge.s	cyl80
	moveq	#0,d1
cyl80   move.w	d0,d2
f_lop	move.l	TimeTab,a0
	move.w	d1,d0
	eor.w	#1,d0
	add.w	d0,a0
	add.w	d0,a0
	tst.w	(a0)
	beq.s	no_write
	movem.l	d0-d2/a0,-(a7)
	bsr	WriteTR
	movem.l	(a7)+,d0-d2/a0
no_write
	addq.l	#1,d1	; next TR
	cmp.w	#160,d1
	bne	f_lop

;	cmp.w	d2,d1	; act TR = end TR ?
;	ble	f_lop	; 

	bra	Complete

;---------------------------------------------------------------------------

DiskInfo:
	bsr	DiskName	; A0 zeigt auf RootBlock

	move.l	$13c(A0),D0	; BitMap-Block lesen (Offset 316)
	cmp.l	#2,D0
	bls	baddos 
	cmp.l	#1759,D0
	bhi	baddos
	bsr	ReadBlock
	addq.l	#4,A0		; Zeiger auf Bitmap in A0

	moveq	#0,D0		; D0 = Blockzaehler
	moveq	#0,D6		; D6 = Summe der freien Bloecke 

maploop	move.l	d0,d1		; aus Blocknr. track und head errechnen
	divu	#22,d1
	move.w	d1,track
	swap	d1
	move.w	#1,head
	cmp.w	#11,d1		; mehr als 11 Sketoren => andere Seite
	blt.s	klein
	clr.w	head
klein
	move.l	WorkTab,A2		
	moveq	#0,D3		; Zaehler fuer Anzahl freier Sektoren
	move.w	#10,d2		; freie Sektoren pro Track zaehlen
trkloop				
	bsr	alloctest
	add.l	d4,d3
	addq.l	#1,d0		; Blocknr. + 1
	move.w	track,d5
	asl.w	#1,d5
	add.w	head,d5
	move.b	D3,0(A2,D5.w)	; Anzahl freier Sektoren merken
	dbf	d2,trkloop

	movem.l	d0-d7/a0-a6,-(a7)
	move.l	#LGREY,d4	; Zahl in D3 ausgeben
	bsr	Display	
	movem.l	(a7)+,d0-d7/a0-a6

	add.l	d3,d6		; Gesamtzahl freier Bloecke erhoehen
	cmp.l	#1760,d0
	blt	maploop

	lea	buf,a0		; in buf steht noch Diskname (max. 30 Chars)
	moveq	#0,D0
txdloop	addq.w	#1,D0		; Textende suchen
	tst.b	(a0)+
	bne	txdloop
	sub.l	#1,A0
	
1$	move.b	#' ',(a0)+	; bis max. Position 30 Spaces einfuegen
	addq.w	#1,d0
	cmp.w	#30,d0
	blt.s	1$

	mulu	#10000,d6
	divu	#1760,d6
	swap	d6
	clr.w	d6
	swap	d6

	lea	prozente(pc),A1
	moveq	#2,D0		; 3 Stellen errechnen
.pcalc	move.w	(A1)+,D1	; Divisor holen
	divu	D1,D6
	ori.b	#$30,D6
	move.b	D6,(A0)+	; Zahl speichern
	clr.w	d6
	swap	d6
	dbf	D0,.pcalc

	lea	freetxt(pc),a1
	moveq	#8,D0
2$	move.b	(A1)+,(A0)+
	dbeq	D0,2$ 
;	clr.b	(a0)

	lea	buf,A0
	moveq	#GREEN,D0
	bsr	Status
	bra	Complete

alloctest:
	movem.l	D0-D1,-(A7)	; D0 = BlockNR. 2 <= D0 <1760
	ext.l	D0
	subq.l	#2,D0
	move.l	D0,D1
	lsr.w	#5,D1	; Byte-Offset= D1/32
	lsl.w	#2,D1	; D2*4
	and.w	#$1F,D0	; BitNR. isolieren
	;
	move.l	(A0,D1.w),D1
	btst	D0,D1
	beq.s	.allocd
	;
	moveq.l	#1,d4
	bra.s	.endalc
	;
.allocd	moveq.l	#0,d4
.endalc	movem.l	(a7)+,D0-D1
	rts

 IFD GER
freetxt	DC.B	"% frei",0
 ELSE
freetxt	DC.B	"% free",0
 ENDC
	EVEN
prozente	DC.W	10000,1000,100
	

; ** liest Block 880 ein und schreibt den Disknamen nach buf,
; ** Abbruch bei ungueltigem Namen
; ** OutputRegs: A0 -> Zeiger auf Puffer mit Block 880
 
DiskName:
	move.w	source,D0
	bsr	seldri
	bsr	InitReadBlock
	move.w	#880,D0
	bsr	ReadBlock	; A0 zeigt auf Sektor 880
	move.l	A0,-(SP)
	lea	$01B0(A0),A0	; A0 zeigt nun DiskNamen	
	lea	buf,A1
	clr.l	(A1)
	moveq	#0,D0		; nun Laengenzaehler
	move.b	(A0)+,D0
	bmi	baddos
	beq	baddos
	cmp.w	#29-1,D0		; mehr als 29 Zeichen fuer den Namen?
	bls.s	1$
	move.w	#29-1,D0		; Maximal 29 Zeichen fuer den Namen
1$	subq.w	#1,D0
	moveq	#0,D2
2$	move.b	(A0)+,D2	
	cmp.w	#32,D2
	blt	baddos
	cmp.w	#176,D2
	bgt	baddos
	move.b	D2,(A1)+	; Char in buf speichern
	dbf	D0,2$
	clr.b	(A1)		; $00 ans buf Ende
	move.l	(SP)+,A0
	rts
DosError
baddos	moveq	#RED,D0
	lea	1$(pc),A0
	bsr	Status
	bra	SpecialERR
 IFD GER
1$	DC.B	"AmigaDOS-Fehler (keine DOS Disk)!",0
 ELSE
1$	DC.B	"AmigaDOS-Error (not a DOS disk)!",0
 ENDC
	EVEN
;---------------------------------------------------------------------------
; Syntax: ShowDir()
; Input : no
; Uses  : all regs
; Output: no
;
; Verwendet die Funktion ReadBlock, es muss daher ein InitReadBlock
; durchgefuehrt werden. Erfolgt hier indirekt ueber Aufruf von DiskName.
; Showdir darf nur ueber enddir verlassen werden, das sonst das Fenster
; nicht restauriert wird!

DATALEN	EQU 40	; ein Datensatz ist 40 Byte lang
FROWS	SET 20	; Anzahl der Eintraege die angezeigt werden 

;	DATASEG
VirWin	WINDOW 0,0,319,199,0,LGREY,0,vr_gad,0
vr_gad	DC.W	4-1
 IFD GER
	BOX_GADGET 80,2,8*8+4,10,LGREY,1,BLUE,<" WEITER ">
 ELSE
	BOX_GADGET 80,2,10*8+4,10,LGREY,1,BLUE,<" CONTINUE ">
 ENDC
	BOX_GADGET 10,2,6*8+4,10,LGREY,2,RED,<" KILL ">
	BOX_GADGET 304,14,14,18,LGREY,3,0,0	; Pfeil 1
	BOX_GADGET 304,161,14,18,LGREY,4,0,0 ; Pfeil 2


DirWin	WINDOW 0,0,319,199,0,LGREY,0,bo_gad,0
bo_gad	DC.W	4-1
	BOX_GADGET 10,2,6*8+4,10,LGREY,1,BLUE,<"PARENT">
	BOX_GADGET 80,2,6*8+4,10,LGREY,2,RED,<" EXIT ">
	BOX_GADGET 304,14,14,18,LGREY,3,0,0	; Pfeil 1
	BOX_GADGET 304,161,14,18,LGREY,4,0,0 ; Pfeil 2
	;	
lintab	DC.W	0,14,319,14
	DC.W	0,179,319,179
	DC.W	0,189,319,189
	DC.W	304,14,304,169
	DC.W	-1
	;
arrowtab
	DC.W	6,0,0,6
	DC.W	0,6,3,6
	DC.W	3,6,3,16
	DC.W	3,16,9,16
	DC.W	9,16,9,6
	DC.W	9,6,12,6
	DC.W	12,6,6,0
	DC.W	-1 

entries	DC.W	0	; Zaehlt die Eintrage insgesamt 
filecount	DC.W	0	; Zaehlt die Files
dircount	DC.W	0	; Zaehlt die Directorys
fbytes	DC.L	0	; Summiert die belegten Bytes
winptr	DC.L	0

blank   	DC.B	"                                     ",0
 IFD GER
dir_statis	DC.B	"%ld Bytes in %d Files, %d Dir(s)",0
 ELSE
dir_statis	DC.B	"%ld bytes in %d files, %d dir(s)",0
 ENDC
	EVEN

	CODESEG

;	***--------------------------------------------------------
;	***
;	*** SHOW VIRUS DUMP
;	*** a0: bootblock data, a1: virusname
;	***
;	***--------------------------------------------------------
ShowVirus:
	movem.l	d1-d7/a0-a6,-(sp)
	move.l	a1,virname
	lea	virbuf(pc),a1	;ptr auf virusbuffer speichern
	move.l	a0,(a1)
 IFNE SOLO
	move.l	#$fffffffe,cop_dummy	;solocopy copperfarben aus
 ENDC
	move.w	#1,dir_active	; sperrt Timeausgabe in VBL
	lea	VirWin,A0
	move.b	WIN_BCOL(A0),D7	; Farbe fuer die Linien holen
	bsr	x_OpenWindow
	move.l	D0,winptr	; Zeiger auf Puffer retten
	;
	lea	lintab,a0	; zeichnet mehrere Linien
	moveq	#3,D5		; 4 Linien ziehen
.nextln	movem.w	(A0)+,D0-D3
	move.b	D7,D4		; Farbe nach D4
	bsr	x_Line
	dbf	D5,.nextln
	;
	move.w	#305,D5		; X-Offset
	moveq	#15,D6		; Y-Offset
	bsr	DrawArrow
	move.w	#-178,D6	; Y-Offset
	bsr	DrawArrow
	;
	moveq	#4,D0
	move.w	#181,D1
	moveq	#RED,D2
	lea	.virusfoundtxt(pc),A0
	bsr	x_Print		; gibt "detected virus" aus
	;
	moveq	#4,D0
	move.w	#191,D1
	moveq	#RED,D2
	move.l	virname(pc),a0
	bsr	x_Print		; gibt virusnamen aus
	;
	moveq	#0,d7		; d7 = aktuelle zeile	
	bsr	ShowVirPage
	;
	TON_BAD
	;
.ckgads	lea	VirWin,A0
	bsr	x_CheckGadgets	; danach 1 <= D0 <= 25
	cmp.w	#1,D0
	beq	.exit
	cmp.w	#2,d0
	beq.s	.kill
	cmp.w	#3,D0
	beq.s	.scrup
	cmp.w	#4,D0
	beq.s	.scrdwn
	bra	.ckgads
	;
.scrdwn	cmp.b	#32-FROWS,d7		; *** SCROLL DOWN
	beq	.ckgads
	addq.l	#1,d7
	bsr	ShowVirPage
	Delay 30000
	bra	.ckgads
	;
.scrup	tst.l	d7		; *** SCROLL UP
	beq	.ckgads
	subq.l	#1,d7
	bsr	ShowVirPage
	Delay 30000
	bra	.ckgads
	;
.kill	bsr	CloseVirWin	; *** KILL
	moveq	#-1,d0
	bra	.end
	;
.exit	bsr	CloseVirWin	; *** EXIT
	moveq	#0,d0
.end	movem.l	(sp)+,d1-d7/a0-a6
	tst.l	d0
	rts
	;
 IFD GER
.virusfoundtxt	DC.B "X-Copy hat einen VIRUS gefunden!",0
 ELSE
.virusfoundtxt	DC.B "X-Copy detected a VIRUS!",0
 ENDC
	EVEN

CloseVirWin:
	lea	DirWin,A1	; veranderte WINDOW-Struktur restaurieren
	add.w	#16,WIN_X1+4(A1)	; X2 erhoehen
	clr.l	WIN_STR(A1)	; Pointer ruecksetzen
	clr.w	dir_active
	RestoreWindow winptr	
 IFNE SOLO
	move.l	#$01800000,cop_dummy
 ENDC
	rts

virbuf		DC.L 0
virname		DC.L 0
	;
	;
;	*** Eine Seite Virusdump ausgeben (ab Zeile D7)
;	***--------------------------------------------
ShowVirPage:
	movem.l	d2-d7,-(sp)
	moveq	#4,d5		; X-Start
	moveq	#16,d6		; Y-Start
	move.l	d7,d3		; Start mit dieser zeile
	moveq	#FROWS-1,D4	; insgesamt FROWS zeilen ausgeben
.loop	move.l	d3,d0
	lsl.w	#5,d0		; ab d3. zeile (*32)
	move.l	virbuf(pc),a0
	add.w	d0,a0
	lea	buf,a1
	bsr	Word2Hex
	lea	buf+4,a1
	move.b	#":",(a1)+
	bsr	StripASC
	lea	buf,a0	
	moveq	#LGREY,d2	; color
	move.l	d5,d0		; x
	move.l	d6,d1		; y
	bsr	x_Print		; print line
	addq.w	#1,d3		; actentry + 1
	addq.w	#8,d6		; Y = Y+8
	dbf	d4,.loop
	movem.l	(sp)+,d2-d7
	rts
	;
	;
;	*** Eine Zeile aus dem Bootblockbuffer in einen ASCII-String conv.
;	***---------------------------------------------------------------
StripASC:
	moveq	#31,d1			;zeilenlänge
.copyline
	move.b	(a0)+,d0		;zeichen aus bootblock nach d0
	cmp.b	#31,d0			;zeichen < space,
	bls.s	.illegalbyte		;dann durch '.' ersetzen
	cmp.b	#159,d0
	bhi.s	.copybyte
	cmp.b	#127,d0
	bls.s	.copybyte
.illegalbyte
	moveq	#".",d0			;wenn illegal, dann "."
.copybyte
	move.b	d0,(a1)+		;zeichen kopieren
.legalbyte
	dbf	d1,.copyline
	clr.b	(a1)			;end of string	
	rts
	;
	;
;	*** Ein Long (d0) in Hex-Ascii (a1) wandeln
;	***----------------------------------------
Word2Hex:
	movem.l	d2-d4,-(sp)
	moveq	#%1111,d4
	moveq	#0,d2
	moveq	#16,d3
.loop	move.l	d0,d1
	subq.b	#4,d3
	lsr.l	d3,d1
	and.w	d4,d1
	move.b	.hextab(pc,d1.w),(a1,d2.w)
	addq.w	#1,d2
	cmp.w	#4,d2
	bne.s	.loop	
	clr.b	(a1,d2.w)
	movem.l	(sp)+,d2-d4
	rts
	;
.hextab	DC.B "0123456789ABCDEF"
	EVEN


DrawArrow:
	lea	arrowtab,A0	; zeichnet Pfeil oben
1$	movem.w	(A0)+,D0-D3
	cmp.w	#-1,D0
	beq	rtrn
	add.w	D5,D0	; X1+305
	add.w	D5,D2	; X2+305
	add.w	D6,D1	; Y1+2
	bpl.s	2$
	neg.w	D1
2$	add.w	D6,D3	; Y2+2
	bpl.s	3$
	neg.w	D3
3$	move.b	D7,D4	; Farbe nach D4
	bsr	x_Line
	bra.s	1$


;	***--------------------------------------------------------
;	***
;	*** SHOW DIRECTORY
;	***
;	***--------------------------------------------------------
ShowDir:
	bsr	DiskName	; fuehrt InitReadBlock durch,liest Namen
				; nach A0 ein, bricht bei NOTDOS ab!
	move.w	#1,dir_active	; sperrt Timeausgabe in VBL
 IFNE SOLO
	move.l	#$fffffffe,cop_dummy	;solocopy copperfarben aus
 ENDC
	lea	DirWin,A0
	move.b	WIN_BCOL(A0),D7	; Farbe fuer die Linien holen
	bsr	x_OpenWindow
	move.l	D0,winptr	; Zeiger auf Puffer retten

	lea	lintab,a0	; zeichnet mehrere Linien
	moveq	#3,D5		; 4 Linien ziehen
nextline
	movem.w	(A0)+,D0-D3
	move.b	D7,D4		; Farbe nach D4
	bsr	x_Line
	dbf	D5,nextline

	move.w	#305,D5		; X-Offset
	moveq	#15,D6		; Y-Offset
	bsr	DrawArrow
	move.w	#-178,D6	; Y-Offset
	bsr	DrawArrow

	lea	DirWin,A1
	sub.w	#16,WIN_X1+4(A1)	; X2 vermindern 
	move.l	WorkTab,A0	; STR_GADGETS aufbauen, WorkTab hat 300 Bytes
	move.l	A0,WIN_STR(A1)	; Pointer auf STR_GADGETS setzen

; ** Folgende Loesung ist fuer den Aufbau einr STR_GADGET-Struktur ist sehr
; ** unsauber, da Wissen uber den Aufbau der Struktur eingesetzt wird.
; ** Wird die Struktur geandert, muss hier unbedingt eine Anpassung erfolgen!
	
	move.w	#FROWS-1,(A0)+	 ; Anzahl der dargestellten Files
	moveq	#5,D0		 ; 5 ist 1.ter moeglicher RET_PAR
	move.w	#16,D1		 ; Y-Start der File-Eintraege

1$	move.l	#$000A0000,(A0)+ ; Laenge der Struktur + X
	move.w	D1,(A0)+	 ; Y-Koordinate
	clr.b	(A0)+
	move.b	D0,(A0)+	 ; Rueckgabewert
	clr.w	(A0)+		 ; Rest loeschen
	addq.w	#8,D1
	addq.w	#1,D0		 ; RET_PAR+1
	cmp.w	#5+FROWS,D0
	blt.s	1$
 
	moveq	#2,D0
	move.w	#181,D1
	moveq	#BLUE,D2
	lea	buf,A0
	bsr	x_Print		; gibt DiskNamem aus
	
	move.l	#880,D0		; 1.ter Block zu lesen = RootBlock
rd_dir	bsr	GetDir		; DirBlock in D0 lesen,D6 Anzahl der Eintraege
	tst.w	D0
	bne	bad_dir		; Fehler, dann Abbruch
	;
	moveq	#1,D0		; X1
	move.w	#189+1,D1	; Y1
	move.w	#318,D2		; X2
	move.w	#198,D3		; Y2
	moveq	#BLACK,D4	; Farbe SCHWARZ = loeschen
	bsr	x_RectFill
	move.w	dircount,-(SP)
	move.w	filecount,-(SP)
	move.l	fbytes,-(SP)
	XPRINTF buf,dir_statis,8
	lea	buf,A0
	moveq	#2,D0		; X1
	move.w	#189+2,D1	; Y1
	moveq	#BLUE,D2
	bsr	x_Print
	moveq	#0,D6
	move.w	entries,D6
	clr.l	D7		; actentry

no_butts	
	btst	#6,$BFE001	; auf erste Maustaste warten
	beq	no_butts	; linke Maustaste gedrueckt  	

pr_entr	movem.l	D6-D7,-(SP)
	bsr	pentries
	movem.l	(SP)+,D6-D7

ck_gads	lea	DirWin,A0
	bsr	x_CheckGadgets	; danach 1 <= D0 <= 25
	
	cmp.w	#1,D0
	beq.s	parentdir
	cmp.w	#2,D0
	beq.s	ok_dir
	cmp.w	#3,D0
	beq.s	scrolldown
	cmp.w	#4,D0
	beq.s	scrollup
	bra.s	new_dir

parentdir
	move.l	parent,D0	; parent wird von GetDir gesetzt
	beq	ck_gads		; stehen schon im ROOT-Dir, kein Parent da 
	bra	rd_dir		; Parent-Directory neu lesen 

scrolldown
	tst.w	D7		; Scrolldown 
	beq	ck_gads
	subq.w	#1,d7
	bra	pr_entr

scrollup	
	cmp.w	D6,D7		; D7 >= entries ?
	bge	ck_gads		; JA
	addq.w	#1,D7
	cmp.w	D6,D7
	bge	ck_gads
	bra	pr_entr

new_dir	subq.w	#5,D0		; 
	add.w	D7,D0		; D7 = actentry
	cmp.w	D6,D0		; 	
	bge	ck_gads		; D0 >= entries
	mulu	#DATALEN,D0	; D0 ist Nummer eines gueltigen Eintrags
	move.l	BlockTab,A0	; Zeiger auf Eintrag errechnen
	add.l	D0,A0
	moveq	#0,D0
	move.w	(A0),D0		; handelt es sich um ein UserDir oder File?
	beq	ck_gads		; es handelt sich um ein File
	bra	rd_dir		; neues UserDir einlesen

ok_dir	bsr	d_cleanup	; Ende, wenn alles OK
	bra	Complete
bad_dir	bsr	d_cleanup	; Ende, wenn Fehler aufgetreten ist.
	bra	baddos

d_cleanup
	lea	DirWin,A1	; veranderte WINDOW-Struktur restaurieren
	add.w	#16,WIN_X1+4(A1)	; X2 erhoehen
	clr.l	WIN_STR(A1)	; Pointer ruecksetzen
	clr.w	dir_active
	RestoreWindow winptr	
 IFNE SOLO
	move.l	#$01800000,cop_dummy
 ENDC
	rts

; ** Ausgabe der Directory-Eintraege
 
pentries
	move.l	BlockTab,A5
	move.w	D7,D0		; D7 = actentry
	mulu	#DATALEN,D0
	add.l	D0,A5		; A5 zeigt auf aktuellen Datensatz

	moveq	#8,D0		; X-Start
	moveq	#16,D1		; Y-Start
	move.w	D7,D3		; Start mit diesem Eintrag
	moveq	#FROWS-1,D4	; insgesamt FROWS Eintraege ausgeben

pr_loop	lea	blank,A0	; wenn kein Eintrag mehr SPACES ausgeben
	cmp.w	entries,D3
	bge.s	pr_it	
	move.l	A5,A0		; Zeiger auf DataSet nach A0
	lea	DATALEN(A5),A5	; Zeiger auf naechsten Datensatz setzen
	moveq	#GREEN,D2	; Farbe fuer FILE	
	tst.w	(A0)+		; A0 zeigt nun auf String	
	beq.s	pr_it		; NULL bei Files
	moveq	#LGREY,D2	; Farbe fuer DIR	
pr_it	movem.l	D0-D1,-(SP)
	bsr	x_Print
	movem.l	(SP)+,D0-D1 
	addq.w	#1,D3		; actentry + 1
	add.w	#8,D1		; Y = Y+8
	dbf	D4,pr_loop
	rts

;-----------------------------------------------------------------------
; Syntax: GetDir()
; Input : D0 -> Blocknr. eines Directory-Blocks
; Uses  : all regs
; Output: D0 = 0 OK, D0=1 Fehler aufgetreten 
;
; Liest alle Eintraege der HASH-Tabelle eines DIR-Blocks, sowie verfolgt
; die Hash-Ketten der gelesenen Bloecke. Die Daten werden ab
; BlockTab (= mfm3 + Laenge der TimeTab) in folgendem Format abgelegt:
; FileHeader: NULL        W, Name + FileLen + $00 , insg. DATALEN Bytes
; UserDir   : BlockNummer W, Name + " (DIR)" +$00 , insg. DATALEN Bytes
;

GetDir:	clr.w	entries		; Zaehler fuer alle Eintraege
	clr.w	filecount	; folgende 3 Variablen dienen nur 
	clr.w	dircount	; statistischen Zwecken
	clr.l	fbytes

	cmp.l	#1759,D0
	bhi	DosError

	move.l	BlockTab,A5	; A5 ist Pointer auf Memorybuffer
	bsr	ReadBlock	; Directroy-Block einlesen, A0 = Pointer
	bsr	LockTrack	; vor freigeben schuetzen
	move.l	b_parent(A0),D0
	move.l	D0,parent 	; Blocknr. des parent-directory speichern
	cmp.l	#1759,D0
	bhi	DosError

	lea	$18(A0),A1	; Start der Hash-Tabelle
	moveq	#71,D7		; maximale Anzahl der HASH-Eintraege

dget_hash
	move.l	(A1)+,D0	; BlockNR aus Hash-Tabelle holen
	beq	dnext_hash	; keine gueltige NR.
	movem.l	A0-A1,-(SP)	; Zeiger in Dir-Block retten

dget_chain
	move.l	D0,oldblk	; Hash-Block lesen, Register werden gerettet

	cmp.l	#1759,D0
	bhi	DosError

	bsr	ReadBlock	; ** A0 -> Pointer auf Hash-Block
	move.l	A5,A1		; BLK untersuchen und in Tabelle eintragen

	moveq	#DATALEN-2,D0	; Dataset mit SPACES auffuellen
1$	move.b	#' ',(A1)+
	dbf	D0,1$
	clr.b	(A1)		; Dataset mit NULL abschliessen
	move.l	A5,A1		; ab A1 neuen Datensatz anlegen 

	cmp.l	#T_SHORT,b_type(A0)
	bne	err_getdir	; ACHTUNG: Fehlerbedingung
	move.l	b_subtype(A0),D0	
	cmp.l	#ST_USERDIR,D0
	bne.s	check_file
	addq.w	#1,dircount
	move.l	oldblk,D0	; bei UserDir Blocknr speichern	
	move.w	D0,(A1)+	; UserDirBlock enthaelt wiederum eine Hash-Tab.
	bsr	copy_name	; Name von A0 nach A1 kopieren
	lea	dirtxt(pc),A2	; das Kuerzel " (DIR)" anhaengen
	moveq	#5,D0
2$	move.b	(A2)+,(A1)+
	dbf	D0,2$
	bra.s	dtst_chain
	;
dirtxt	DC.B	" (DIR)",0
flentxt	DC.B	"%6ld",0
	EVEN

check_file
	cmp.l	#ST_FILE,D0
	bne	err_getdir	; ACHTUNG: Fehlerbedingung
	addq.w	#1,filecount
	clr.w	(A1)+		; NULL, keine BlockNR, Kennung fuer File
	bsr.s	copy_name	; Name von A0 nach A1 kopieren
	move.l	324(a0),D0	; Filelaenge in Bytes nach D0
	add.l	D0,fbytes	; zur Gesamtzahl addieren	
	move.l	D0,-(SP)	; Filelaenge in buf drucken
	XPRINTF buf,flentxt,4
	lea	DATALEN-7(A5),A1	; Filelaenge an Position DATALEN-7 schreiben
	lea	buf,A2
	moveq	#5,D0		; 6 Bytes kopieren
1$	move.b	(A2)+,(A1)+
	dbf	D0,1$	

dtst_chain
	lea	DATALEN(A5),A5	; und Datensatzzeiger erhoehen
	move.l	b_hash(A0),D0	; liegt eine Hash-Kette vor?
	bne	dget_chain	; ja, Kette verfolgen

	movem.l	(SP)+,A0-A1	; mit Dir-Block weitermachen
dnext_hash
	dbf	D7,dget_hash

	moveq	#0,D0		; Normales Ende
	rts
err_getdir
	moveq	#1,D0		; Fehler aufgetreten
  	rts

copy_name
	move.l	A2,-(SP)
	addq.w	#1,entries	; Eintraege erhoehen
	lea	$01B0(A0),A2	; A2 zeigt nun auf File/Dir-Namen	
	moveq	#0,D0	; D0 nun Laengenzaehler
	move.b	(A2)+,D0
	bmi	baddos
	beq	baddos
	cmp.w	#29,D0	; mehr als 30 Zeichen fuer den Namen?
	bhi	baddos
	subq.w	#1,D0
1$	move.b	(A2)+,(A1)+	; Namen kopieren
	dbf	D0,1$
	move.l	(SP)+,A2
	rts

;---------------------------------------------------------------------
;
;	CheckDisk
;
CUPPER	bsr.s	rCUPPER
	bra	cbreak
	;
CLOWER	bsr.s	rCLOWER
	bra	cbreak
	;
rCUPPER	clr.w	head
	bsr	side0	; Copy only side0		
	bra.s	c0
	;
rCLOWER	move.w	#1,head
	bsr	side1
	clr.w	s_head
c0	move.w	#2,tries
c1	move.l	mfm1,A0
	bsr	FastRead
	bsr	WaitFlag
	beq.s	1$
	clr.l	(A0)	; Puffer ungueltig
1$	bsr	CheckTrack	; D0 != 0 => ERRORNumber in D0 !
	tst.l	D0
	beq.s	2$
	subq.w	#1,tries
	bne.s	c1
2$	move.l	D0,D3
	bra	StatOut
	
CheckDisk:
	tst.w	s_track		; nur bei starttrack/head 00 viruscheck!
	bne.s	checkdiskloop
	tst.w	s_head
	bne.s	checkdiskloop
	bsr	VirusCheck
checkdiskloop
	move.w	source,D0
	bsr	seldri
	clr.w	err1
	clr.w	err2

	move.w	side,D0
	beq.s	CBOTH
	cmp.w	#1,D0
	beq	CUPPER
	bra	CLOWER
CBOTH	tst.w	s_head		; auf ersten Cylinder nur side1
	bne	CLOWER		; kopieren
	move.w	track,D0
	cmp.w	endtrack,D0
	blt.s	1$
	tst.w	endhead		; auf letzten Cylinder nur side0
	beq	CUPPER		; kopieren

1$	bsr	side0
	move.l	mfm1,A0
	bsr	FastRead
	bsr	WaitFlag
	beq.s	cok1
	clr.l	(A0)	; Buffer1 ungueltig => No Sync

cok1	bsr	side1
	move.l	mfm2,A0
	bsr	FastRead
	clr.w	head		

	move.l	mfm1,A0
	bsr	CheckTrack	; D0 != 0 => ERRORNumber in D0 !
	move.w	D0,err1
	move.l	D0,D3
	bsr	StatOut
	bsr	WaitFlag	; Wait for reading Side1
	beq.s	cok2
	move.l	mfm2,A0
	clr.l	(A0)		; Puffer2 ungueltig => No Sync

cok2
;	tst.w	track
;	bne.s	.notrk0
;	move.l	mfm2,a0
;	bsr	VirusCheck
	;
.notrk0	move.w	#1,head		
	move.l	mfm2,A0
	bsr	CheckTrack
	move.w	D0,err2
	move.l	D0,D3
	bsr	StatOut

	tst.w	err1
	beq.s	1$
	bsr	rCUPPER
1$	tst.w	err2
	beq.s	cbreak
	bsr	rCLOWER	

cbreak	bsr	BreakStep
	bra	checkdiskloop

; ** CheckSum uber einen Block errechnen
; ** wird von Ram-Copy,Optimize und Format gerufen

BlockSum
	move.l	A0,-(SP)
	clr.l	(A1)	; alte Checksum loeschen
	moveq	#$7F,D1
	moveq	#0,D0
1$	add.l	(A0)+,D0
	dbf	D1,1$
	neg.l	D0
	move.l	D0,(A1)	; neue Checksum eintragen
	move.l	(SP)+,A0
	rts



;-------------------------------------------------------------
;	
;	VIRUS CHECKER
;	-------------
;	a0: zeigt auf mfm-daten
;	in d0 wird fehlercode zurückgegeben (wenn != 0, zu Complete!
;	mfm3 wird als buffer zum dekodieren benutzt
;
;-------------------------------------------------------------
VirusCheck:
	movem.l	d1-d7/a0-a6,-(sp)
	;
	lea	.viruschktxt(pc),a0
	moveq	#GREEN,d0
	bsr	Status
	;
	move.w	source,d0	; select source drive
	bsr	seldri
	clr.w	track
	move.w	#1,head		; select head and track
	bsr	side1
	bsr	Go2S_Track
	move.l	mfm1,a0		; read bootblock track
	clr.l	(a0)
	bsr	FastRead
	bsr	WaitFlag	; Wait for reading Side1
	beq.s	.ok
	clr.l	(a0)		; Puffer2 ungueltig => No Sync
	bra	.error2
	;
.ok	move.l	a0,a1
	move.l	mfm3,a0
	move.l	a0,.buf
;	clr.l	(a0)
	move.l	a1,ptr
	bsr	DecodeTrack		;track dekodieren
;	tst.w	d0			;fehler ?
;	bne.s	.end			;dann kein check
	move.l	.buf(pc),a0		;"DOS0" bootblock ?
;	cmp.l	#"DOS"<<8,(a0)
;	bne	.end			;nein, dann kein check
	;
	move.l	mfm3,a1			;bootblock auf virus prüfen
	lea	virustab(pc),a0
	moveq	#-1,d2
.seek	move.w	(a0)+,d0
	tst.w	d0
	beq	.end			;keinen virus gefunden
	move.l	(a0)+,d1		;matchlong
	addq.l	#1,d2
	cmp.l	(a1,d0.w),d1
	bne.s	.seek
.found:	move.l	d2,d0			;virusnummer in d0
	;
	lea	virustext(pc),a0	;virusnamen bestimmen
	tst.l	d0
	beq.s	.first
.getnam	tst.b	(a0)+
	bne.s	.getnam
	subq.l	#1,d0
	bne.s	.getnam
.first	move.l	a0,a1			;a1 = virusname
	;
	move.l	mfm3,a0
	bsr	ShowVirus
	beq	.end
	;
	move.w	target,oldtarget
	move.w	source,oldsource
	move.b	Verify,oldverify
	clr.b	Verify
	move.w	source,target
	clr.w	source
	move.w	target,D0
	bsr	seldri
	;
	;
	btst	#3,$bfe001		;write prot ?
	beq	.error
	;
;	clr.w	s_track
;	move.w	#1,head
;	bsr	Go2S_Track
;	move.w	s_track,track
;	move.w	#1,vchk_active
	bsr	FFormat
	;
	clr.w	vchk_active
	move.w	oldtarget,target
	move.w	oldsource,source
	move.b	oldverify,Verify
	bra.s	.end
	;
.error	move.w	oldtarget,target
	move.w	oldsource,source
	move.b	oldverify,Verify
	moveq	#0,d0
	move.w	source,d0
	lsr.w	#1,d0
	movem.l	(sp)+,d1-d7/a0-a6
	bra	wprotON
	;
.error2	lea	.chkerrtxt(pc),a0
	moveq	#RED,d0
	bsr	Status
.end	bsr	StatusClear
	movem.l	(sp)+,d1-d7/a0-a6
	rts
	;
.buf	DC.L	0
 IFD GER
.viruschktxt	DC.B "Ueberpruefe Virus-Infektion...",0
 ELSE
.viruschktxt	DC.B "Checking for virus-infection...",0
 ENDC
 IFD GER
.chkerrtxt	DC.B "Lesefehler bei Virus-Ueberpruefung",0
 ELSE
.chkerrtxt	DC.B "Read-error during virus-check",0
 ENDC
	EVEN

virustab:
	DC.W $ca,$2b79,$0007    ;sca,lsd,aek,dag,ice,graffiti
	DC.W 150,$4eae,$fd9c	;dasa,byte warrior
	DC.W $4c,$48e7,$7f7f	;byte bandit
	DC.W $190,$2d48,$0226	;byte bandit 2
	DC.W $82,$2d49,$fe3a	;obelisk
	DC.W  82,$0c68,$424d	;disk doktors
	DC.W $1A4,$00FC,$06DC	;disk doktors 2
	DC.W $b8,$0c40,$f300	;ass protector
	DC.W $ec,$0007,$fbdc	;gadaffi
	DC.W $24,$0007,$e63a	;warhawk
	DC.W $ca,$0007,$eff0	;old north star
	DC.W 208,$0007,$ec0e	;new north star
	DC.W $8c,$0007,$fb4c	;pentagon slayer
	DC.W $34,$0007,$e060	;revenge, sendarian
	DC.W $7c,$0007,$0320	;time bomb
	DC.W $03e,$0007,$ec58	;16bit crew
	DC.W $0aa,$0007,$f4c8	;microsystems
	DC.W $036,$0007,$F0AA	;blackflash
	DC.W $15C,$32BC,$4AFC	;joshua
	DC.W $21c,$0007,$ed36	;uf
	DC.W $22a,$c020,$0f19	;vkill
	DC.W $22,$4E75,$70FF	;aids (hide)
	DC.W $64,$0028,$00d8	;kauki
	DC.W $13a,$4afc,$2349	;scarface
	DC.W $a4,$0007,$ef2a	;disk herpes (phantasm)
	DC.W $22,$00fc,$00d2	;fastload bytewarrior
	DC.W $60,$4afc,$0200	;turk
	DC.W $30,$33fc,$4000	;revenge boot
	DC.W $C0,"PS","WC"	;paramount
	DC.W $37A,$B159,$D041	;clist
	DC.W $68,$0007,$EC4C	;butonic's
	DC.W $5E,$0080,$4E40	;julie
	DC.W $10,$0c79,$444f	;alien new beat
	DC.W $7a,$ffff,$fe38	;LADS
	DC.W $f6,$b3e8,$ffe4	;australian parasite
	DC.W $126,$0a10,$00ff	;target
	dc.w $86,$32bc,$4afc	;extreme
	dc.w $364,$41fa,$fcca	;f.a.s.t.
	DC.W $38,$10C0,$B1C9	;FUCK OFF VIRUS!
	DC.W $54,"CC","CP"	;CCCP Virus
	dc.w $1c,$43f9,$0007	;pentagon circle
	dc.w $42,$343c,$0352	;lamer 1
	dc.w $42,$45fa,$0369	;lamer 2
	dc.w $42,$45fa,$0365	;lamer 3
	dc.w $1A,$123a,$03d3	;lamer 4
	dc.w $42,$45fa,$0350	;lamer 5
	dc.w $10,$432e,$0007	;hcs virus revenge I/II
	dc.w $38,$4afc,$0007	;gx team virus
	dc.w $60,$12d8,$51c8	;claas abraham
	dc.w $94,$203c,$1113	;no name virus terminator
	dc.w $ce,$0839,$0003	;termigator
	dc.w $29c,$e6de,$0879	;coder virus jos
	dc.w 12,$0cb9,$4e75	;hilly virus ali
	dc.w $46,$0839,$0007	;super boy
	dc.w $28c,$257c,$0007	;opapa
	dc.w $18,$ffe8,$43f9	;revenge v12
	DC.W	$19C		; aids
	DC.L	$C9A444F
	DC.W	$10		; blow job
	DC.L	$5B302E35
	DC.W	$58		; coders nightmare
	DC.L	$2027434F
	DC.W	$18		; forpip
	DC.L	$2D20464F
	DC.W	$16		; gremlins
	DC.L	$CAE0007
	DC.W	$16		; gx team
	DC.L	$610002F8
	DC.W	$390		; hcs 4220
	DC.L	$34323230
	DC.W	$3CA		; icebreakers
	DC.L	$42524541
	DC.W	$37A		; lame style uk
	DC.L	$B159D041
	DC.W	$22E		; paradox
	DC.L	$224C4F47
	DC.W	$12		; saddam hussein
	DC.L	$32303030
	DC.W	$324		; supply team
	DC.L	$20537570
	DC.W	8		; sca strain
	DC.L	$43485721
	DC.W	$1E		; amigafreak virus
	DC.L	$414D4947
	DC.W	$1F2		; destructor
	DC.L	$20446573
	DC.W	$278		; digital emotions
	DC.L	$44494749
	DC.W	$328		; disk guard
	DC.L	$49534B47
	DC.W	$36E		; f.a.s.t.
	DC.L	$A1A0031
	DC.W	$3F2		; f.i.c.a.
	DC.L	$462E492E
	DC.W	$1A		; incognito
	DC.L	$6900000
	DC.W	$1EE		; jitr
	DC.L	$4A495452
	DC.W	$50		; joshua 2
	DC.L	$22680018
	DC.W	$54		; mca
	DC.L	$2A49203C
	DC.W	$3C		; megamaster
	DC.L	$CA9FFFE
	DC.W	$18		; morbid angel
	DC.L	$204D6F72
	DC.W	$10		; rene
	DC.L	$70BA6122
	DC.W	$3AE		; pseudo
	DC.L	$323C01B1
	DC.W	$32		; switch off
	DC.L	$10100A00
	DC.W	$28		; mt
	DC.L	$91C92008
	DC.W	12		; vkill 2
	DC.L	$705E6122
	DC.W	$366		; pentagon 2
	DC.L	$2050656E
	DC.W	$2DA		; pentagon 3
	DC.L	$656E7461
	DC.W	$3D0		; phantastograph
	DC.L	$74736874
	DC.W	$20		; telstar
	DC.L	$4F522056
	DC.W	$3C6		; traveller
	DC.L	$54726176
	DC.W	0

virustext:
	DC.B "SCA,LSD,AEK,DAG,Ice,Graffiti",0
	DC.B "Dasa,Byte warrior",0
	DC.B "Byte Bandit",0
	DC.B "Byte Bandit 2",0
	DC.B "Obelisk",0
	DC.B "Disk Doktors",0
	DC.B "Disk Doktors 2",0
	DC.B "Ass Protector",0
	DC.B "Gadaffi",0
	DC.B "Warhawk",0
	DC.B "Old North Star",0
	DC.B "New North Star",0
	DC.B "Pentagon Slayer",0
	DC.B "Revenge, Sendarian",0
	DC.B "Time Bomb",0
	DC.B "16Bit Crew",0
	DC.B "Microsystems",0
	DC.B "Blackflash",0
	DC.B "Joshua",0
	DC.B "UF",0
	DC.B "VKill",0
	DC.B "AIDS",0
	DC.B "Kauki",0
	DC.B "Scarface",0
	DC.B "Disk Herpes (Phantasm)",0
	DC.B "Fastload Bytewarrior",0
	DC.B "Turk",0
	DC.B "Revenge Boot",0
	DC.B "Paramount",0
	DC.B "CList",0
	DC.B "Butonic's",0
	DC.B "Julie",0
	DC.B "Alien New Beat",0
	DC.B "LADS",0
	DC.B "Australian Parasite",0
	DC.B "Target",0
	DC.B "Extreme",0
	DC.B "F.A.S.T.",0
	DC.B "unknown virus!",0
	DC.B "CCCP Virus",0
	DC.B "Pentagon Circle",0
	DC.B "Lamer Exterminator 1",0
	DC.B "Lamer Exterminator 2",0
	DC.B "Lamer Exterminator 3",0
	DC.B "Lamer Exterminator 4",0
	DC.B "Lamer Exterminator 5",0
	DC.B "HCS Virus Revenge I/II",0
	DC.B "GX Team Virus",0
	DC.B "Claas Abraham",0
	DC.B "No Name Virus Terminator",0
	DC.B "Termigator",0
	DC.B "Coder Virus Jos",0
	DC.B "Hilly Virus Ali",0
	DC.B "Super Boy",0
	DC.B "Opapa",0
	DC.B "Revenge v12",0
	DC.B "AIDS",0
	DC.B "Blow Job",0
	DC.B "Coders Nightmare",0
	DC.B "Forpip",0
	DC.B "Gremlins",0
	DC.B "GX Team",0
	DC.B "HCS 4220",0
	DC.B "Icebreakers",0
	DC.B "Lame Style UK",0
	DC.B "Paradox",0
	DC.B "Saddam Hussein",0
	DC.B "Supply Team",0
	DC.B "SCA Strain",0
	DC.B "Amigafreak Virus",0
	DC.B "Destructor",0
	DC.B "Digital Emotions",0
	DC.B "Disk Guard",0
	DC.B "F.A.S.T.",0
	DC.B "F.I.C.A.",0
	DC.B "Incognito",0
	DC.B "Jitr",0
	DC.B "Joshua 2",0
	DC.B "MCA",0
	DC.B "Megamaster",0
	DC.B "Morbid Angel",0
	DC.B "Rene",0
	DC.B "Pseudo",0
	DC.B "Switch Off",0
	DC.B "MT",0
	DC.B "VKill 2",0
	DC.B "Pentagon 2",0
	DC.B "Pentagon 3",0
	DC.B "Phantastograph",0
	DC.B "Telstar",0
	DC.B "Traveller",0
	DC.B 0
	EVEN

	BSSSEG
oldsource	DS.W 1
oldtarget	DS.W 1
oldverify	DS.W 1
vchk_active	DS.W 1
	CODESEG
;-------------------------------------------------------------
;	
;	Fast Format
;
; ** bei SOLO-Copy werden die Format-Routinen aus Platzgruenden weg-
; ** gelassen

QFormat
	move.w	target,D0
	bsr	seldri

	clr.w	s_track		; kompletten Track0 formatieren
	move.w	#1,head
	bsr	Go2S_Track
	move.w	s_track,track
	bsr.s	FFormat

	cmp.w	#INSTALL,mode
	beq	Complete

;	move.w	#1,head
;	bsr	FFormat

	move.w	#40,s_track	; kompletten Track40 formatieren
	bsr	Go2S_Track
	move.w	s_track,track
	bsr.s	FFormat

;	clr.w	head
;	bsr	FFormat

	bra	Complete	
	
FFormat:
	tst.w	track
	bne.s	1$
	tst.w	head
	bne	makeboot	; Bootblock auf LOWER SIDE schreiben
1$	cmp.w	#40,track
	bne.s	2$
	tst.w	head
	bne	makeroot	; Rootblock schreiben
2$	bsr	Header		; Headler-LONG + CHECKSUM korrigieren

form_it	move.w	target,D0
	bsr	seldri
	move.l	mfm1,A0
	move.l	A0,ptr
	move.l	A0,ver3	; fuer Verify
	bsr	FastWrite
	bsr	WaitFlag
	moveq	#0,D3	; zunaechst mal OK ausgeben
	bsr	StatOut
	moveq	#1,D0	; Verify nur fuer aktuelle Seite
	bra	FastVerify

fillmfm2
	move.l	mfm2,A0
1$	move.l	#'(W)X',(A0)+
	move.l	#'COPY',(A0)+
	dbf	D0,1$
	move.l	mfm2,A0	; Source 
	move.l	mfm1,A1	; Destination
	bra	CodeTrack

; ** Initialisiert ROOT und BITMAP Block

makeroot
	move.l	mfm2,A0
	move.l	A0,A1
	move.w	#1024/4,D0
1$	clr.l	(A1)+		; beide Bloecke loeschen
	dbf	D0,1$
	move.w	#$0002,2(A0)
	move.w	#$0048,14(A0)
	move.w	#$0001,$13A(A0)
	move.w	#$0371,$13E(A0)	; Pointer auf BitMap Block
	move.w	#$0001,$1FE(A0)
	lea	$1B1(A0),A1	; DiskName eintragen
	lea	buf,A2		; Name steht in buf
	moveq	#0,D0
2$	move.b	(A2)+,D1
	beq.s	3$		; Stringende
	move.b	D1,(A1)+
	addq.l	#1,D0
	bra.s	2$
3$	move.b	D0,$1B0(A0)	; Stringlaenge	

	lea	$204(A0),A1	; Bitmap erstellen
	moveq	#-1,D1	; alle Bloecke frei
	moveq	#$E0/4-2,D0
4$	move.l	D1,(A1)+
	dbf	D0,4$
	move.w	#$3FFF,D0
	move.w	D0,$272(A0)	; nur ROOT und BITMAP belegen, 
	move.w	D0,$2DC(A0)	; am Ende fehlen 2 Bit!

	lea	$14(A0),A1	; CheckSum Position
	bsr	BlockSum	; Checksum fuer ROOT!
	lea	$200(A0),A0	
	move.l	A0,A1		; CheckSum Position	
	bsr	BlockSum	; Checksum fuer BITMAP
	bra	new_dat



installprgptr	DC.L boot_start
endinstallptr	DC.L boot_end

;	*** makeboot
;	***----------
makeboot:
	move.l	mfm1,ptr
	move.l	ptr,a0
	bsr	FastRead
	bsr	WaitFlag
	move.l	mfm2,a0
	move.l	ptr,a1
	bsr	DecodeTrack

	bsr	ChooseBoot

	move.l	mfm2,A0
	move.l	installprgptr,A1
	move.l	endinstallptr,A2
1$	move.l	(A1)+,(A0)+
	cmp.l	A2,A1
	blt	1$
	;
	move.l	mfm2,A0		; Pruefsumme uber BOOTBLOCK errechnen	
	move.l	installprgptr,a1
;	tst.l	8(a1)		;noboot-bootblock ?
;	beq.s	new_dat
	;
	move.l	A0,A1
	moveq	#0,D1
	move.w	#255,D0		; 1024 Bytes
2$	move.l	(A1)+,D2	; Carry Flag 0, X-Flag unbeeinflusst
	add.l	D2,D1
	bcc.s	3$
	addq.l	#1,D1
3$	dbf	D0,2$
	addq.l	#1,D1
	neg.l	D1
	move.l	D1,4(A0)	; Pruefsumme ablegen
new_dat	move.l	mfm2,A0		; MFM-kodieren und schreiben 
	move.l	mfm1,A1
	bsr	CodeTrack
	bsr	form_it
	tst.w	vchk_active
	bne	rtrn
	move.w	#1024/8,D0	; Puffer mit alten Werten fuellen
	bra	fillmfm2

;	*** Header
;	***--------
Header:
	move.l	mfm1,A3
	moveq	#0,D7		; Headerlong erstellen
	move.w	track,D7	; Tracknummer errechnen
	add.w	D7,D7
	move.w	head,D1
	eor.w	#1,D1
	add.w	D1,D7
	or.w	#$FF00,D7	; Amigaformat
	swap	D7
	moveq	#0,D5		; aktuelle SecNR	
	moveq	#$0B,D6		; secs till end
	;
1$	moveq	#0,D0
	move.w	D5,D0		; SecNum
	move.b	D6,D0		; Secs till end
	or.l	D7,D0
	lea	8(A3),A1
	bsr	codelong
	moveq	#0,D0
	bsr	codelong
	lea	8(A3),A1	; Checksum ueber Header
	moveq	#40,D1		; = 40 Bytes berechnen
	bsr	checksum
	lea	48(A3),A1
	bsr	codelong	; D0 kodieren ab A1
	add.w	#$440,A3
	add.w	#$0100,D5	; aktuelle Sektornr + 1
	subq.w	#1,D6
	bne	1$
	rts


;	*** cancel = ChooseBoot()
;	***---------------------
ChooseBoot:
	movem.l	d1-d7/a0-a6,-(sp)
	moveq	#GREEN,d0
	lea	.cboottxt(pc),a0
	bsr	Status
	;
.getkey	bsr	GetKey
	cmp.b	#"D",d0
	beq.s	.std
	cmp.b	#"d",d0
	beq.s	.std
	cmp.b	#"N",d0
	beq.s	.noboot
	cmp.b	#"n",d0
	beq.s	.noboot
	cmp.b	#"X",d0
	beq.s	.xcopy
	cmp.b	#"x",d0
	bne.s	.getkey
	;
.xcopy	lea	novirus_start(pc),a0
	move.w	#novirus_end-novirus_start-1,d0
	moveq	#1,d2
	bra.s	.copyboot
	;
.std	lea	standard_start(pc),a0
	move.w	#standard_end-standard_start-1,d0
	moveq	#1,d2
	bra.s	.copyboot
	;
.noboot	lea	noboot_start(pc),a0
	move.w	#noboot_end-noboot_start-1,d0
	moveq	#0,d2
.copyboot
	lea	boot_start,a1
	move.l	a1,-(sp)
	move.w	#1024/4-1,d1
.fill0	clr.l	(a1)+
	dbf	d1,.fill0
	move.l	(sp)+,a1
.cboot	move.b	(a0)+,(a1)+		;copy bootblock
	dbf	d0,.cboot
	;
	tst.l	d2			;checksum ?
	beq.s	.nosum
	lea	boot_start,a0
	move.l	A0,A1
	moveq	#0,D1
	move.w	#255,D0		; 1024 Bytes
.sloop	move.l	(A1)+,D2	; Carry Flag 0, X-Flag unbeeinflusst
	add.l	D2,D1
	bcc.s	.scarry
	addq.l	#1,D1
.scarry	dbf	D0,.sloop
	addq.l	#1,D1
	neg.l	D1
	move.l	D1,4(A0)	; Pruefsumme ablegen

.nosum	moveq	#0,d0
	bra.s	.done
	;
.cancel	moveq	#-1,d0
	;
.done	move.l	d0,-(sp)
	bsr	StatusClear
	move.l	(sp)+,d0
	movem.l	(sp)+,d1-d7/a0-a6
	tst.l	d0
	rts
	;
	;
.cboottxt	DC.B "Bootblock (X)Copy, (D)OS, (N)oboot ?",0
		EVEN
	BSSSEG
boot_start	DS.B 1028
boot_end
	
	CODESEG
		
;**********************************
;	NOVIRUS BootBlock
;**********************************

novirus_start:

InitRastPort	= -198
InitBitMap	= -390
Text		= -60
Move		= -240
SetAPen		= -342

clist	= $7EE80
plane	= $7EF00

go	DC.B	"DOS",0
 IFD GER
	DC.L	$26ba261a
 ELSE
	DC.L	$71e12c82
 ENDC
	DC.L	880
	lea	GfxName1(PC),A1
	moveq	#0,D0
	jsr	-552(A6)	; Open GfxLib
	move.l	D0,A6
	move.l	raspor(PC),A1	; RasPor => Size $4E
	jsr	InitRastPort(A6)
	move.l	mymap(PC),A0	; BitMapPTH
	moveq	#1,D0	; Depth
	move.l	#320,D1	
	moveq	#8,D2
	jsr	InitBitMap(A6)
	move.l	raspor(PC),A0
	move.l	mymap(PC),A1
	move.l	A1,4(A0)	; BitMapPTH in RastPort setzten

	move.l	#plane,A0
	move.l	A0,8(A1)	; PlanePTH in BitMap setzen
	move.w	#8*40/4,D0
2$	clr.l	(A0)+
	dbf	D0,2$

	move.l	raspor(PC),A1
 IFD GER
	moveq	#55,D0		; X
 ELSE
	moveq	#35,d0
 ENDC
	moveq	#6,D1		; Y
	jsr	Move(A6)
	move.l	raspor(PC),A1
	lea	mess(PC),A0
 IFD GER
	moveq	#25,D0		; StringLen
 ELSE
	moveq	#22,d0
 ENDC
	jsr	Text(A6)

	move.l	#clist,A0
	move.l	A0,A2
	lea	coplist1(pc),A1	; CopList kopieren
	moveq	#30,D0
3$	move.l	(A1)+,(A0)+
	dbf	D0,3$

	lea	$DFF000,A0
	move.l	A2,$80(A0)	; eigene CopperListe an
	clr.w	$88(A0)	
	move.w	#$8300,$96(A0)	; BitPlane DMA ON
	moveq	#12,D1
8$	nop			; Warteschleife
	dbf	D0,8$
	dbf	D1,8$
	move.l	38(A6),$80(A0)	; CopperListe an, GfxBase->CopInit
	clr.w	$88(A0)	

	move.l	$04,A6
	lea	dostx(PC),A1
	jsr	-96(A6)
	tst.l	D0
	beq	9$
	move.l	D0,A0
	move.l	22(A0),A0
	moveq	#0,D0
	rts
9$	moveq	#-1,D0
	rts
dostx	DC.B	"dos.library",0
	EVEN
raspor	DC.L	$7ee00	; Size $4E
mymap	DC.L	$7ee50

ZCOL	equ $00f9

coplist1
	DC.W	$2001,$FFFE,$0100,$0200
	DC.W	$008E,$0581,$0090,$40C1,$0092,$0038,$0094,$00D0
	DC.W	$0102,$0000,$0104,$0024
	DC.W	$0108,$0000,$010A,$0000
	DC.W	$00E0,$0007,$00E2,$EF00
	DC.W	$7F0F,$FFFE,$0180,ZCOL,$0182,ZCOL,$8001,$FFFE,$0180,$0114
	DC.W	$8C0F,$FFFE,$0100,$1200
	DC.W	$940F,$FFFE,$0100,$0200
	DC.W	$A00F,$FFFE,$0180,ZCOL
	DC.W	$A10F,$FFFE,$0180,$0000
	DC.W	$FFFF,$FFFE

GfxName1	DC.B	"graphics.library"
	DC.B	0,$FF
 IFD GER
mess	DC.B	"KEIN VIRUS AUF BOOTBLOCK!"
 ELSE
mess	DC.B	"NO VIRUS ON BOOTBLOCK!"
 ENDC
	DC.B	0,$ff
novirus_end:
	EVEN


;**********************************
;	STANDARD/NOBOOT BootBlock
;**********************************
noboot_start:
	DC.B	'DOS',0
	DC.L	0,0
noboot_end:

standard_start:
	DC.B	'DOS',0
	DC.L	$C0200F19
	DC.L	880
	;
	lea	.dosname(pc),a1
	jsr	-$60(a6)
	tst.l	d0
	beq.s	.error
	move.l	d0,a0
	move.l	$16(a0),a0
	moveq	#0,d0
.done	rts
	;
.error	moveq	#-$1,d0
	bra.s	.done
	;
.dosname	DC.B	'dos.library',0
standard_end:
	EVEN



;------------------------------------------------------------------------

 IFEQ SOLO
ReInitDrives:
	lea	ondrives,a5
	moveq	#0,d7
.loop	tst.b	(a5)+
	beq.s	.next
	move.l	d7,d0
	bsr	td_DiskChange
.next	addq.b	#1,d7
	cmp.b	#5,d7
	bne	.loop
	rts


;	*** Seek0(drive)
;	***        d0
;	***-------------
td_Seek0:
	bsr	OpenTrackdisk
	bne.s	.nodrv
	;
	lea	mydiskReq,a1		;** Disk eingelegt ?
	move.w	#14,28(a1)		;TD_CHANGESTATE
	move.l	4,a6
	move.l	a1,-(sp)
	jsr	_DoIO(a6)
	move.l	(sp)+,a1
	tst.l	32(a1)
	bne.s	.nodisk
	;
	lea	mydiskReq,a1		;** Auf Track 0 seeken
	move.l	a1,-(sp)
	move.w	#10,28(a1) 		;TD_SEEK
	clr.l	44(a1)
	move.l	4,a6
	jsr	_DoIO(a6)
	move.l	(sp)+,a1
.nodisk	bsr	CloseTrackdisk	
	;	
.nodrv	rts


;	*** bool = DiskInserted (Drive)
;	***                      d0.B
;	*** ---------------------------
td_DiskInserted:
	bsr	OpenTrackdisk
	bne.s	.notd
	;
	lea	mydiskReq,a1
	move.l	a1,-(sp)
	move.w	#14,28(a1) 		;TD_CHANGESTATE
	move.l	4,a6
	jsr	_DoIO(a6)
	move.l	(sp)+,a1
	move.l	32(a1),-(sp)		;io_Actual
	bsr	CloseTrackdisk	
	move.l	(sp)+,d0
	tst.l	d0
.exit	rts
	;
.notd	moveq	#-1,d0
	bra.s	.exit


;	*** DiskChange (Drive)
;	***             d0.B
;	*** ------------------
td_DiskChange:
	move.b	d0,.drive
	and.l	#$ff,d0
	
	bsr	td_DiskInserted
	bne	.nodos
	;
	move.l	4,a6
	lea	.dosname(pc),a1
	moveq	#0,d0
	jsr	-$228(a6)	;openlib
	lea	.dosbase(pc),a0
	move.l	d0,(a0)
	beq.s	.nodos
	bsr.s	.initport
	tst.l	d0
	bne.s	.noport
	lea	myport,a1
	jsr	-$162(a6)	;AddPort

	move.l	.dosbase(pc),a6
	lea	.devname(pc),a0
	move.b	.drive,2(a0)
	add.b	#"0",2(a0)
	move.l	a0,d1
	jsr	-$ae(a6)	;deviceproc
	lea	.devproc(pc),a0
	move.l	d0,(a0)
	beq.s	.nodev

	moveq	#-1,d0
	bsr	.sendpacket
	moveq	#0,d0
	bsr	.sendpacket

	lea	myport,a1
	jsr	-$168(a6)	;RemovePort

.nodev	move.l	4,a6
	moveq	#0,d0		;d0 loeschen
	move.b	.signal(pc),d0
	jsr	-$150(a6)	;FreeSignal
.noport	move.l	4,a6
	move.l	.dosbase(pc),a1
	jsr	-$19e(a6)	;CloseLib
.nodos	moveq	#0,d0		;no error
	rts

.initport:
	movem.l a3/a6,-(a7)
	moveq	#-1,d0		;-1 fuer neues signal
	move.l	4,a6		;
	jsr	-$14a(a6)	;AllocSignal
	lea	myport,a3
	lea	.signal(pc),a0
	move.b	d0,(a0)
	move.b	d0,15(a3)	;mp_SigBit
	cmp.b	#-1,d0
	beq.s	.initerr 	;kein signal frei
	;
	move.b	#4,8(a3)	;ln_Type  = NT_MSGPORT
	move.l	#.portname,10(a3)	;ln_Name  = portname
	move.b	#0,9(a3)	;ln_Pri   = priority
	;
	move.b	#0,14(a3)	;mp_FLAGS = PA_SIGNAL
	sub.l	a1,a1		;my_task = 0
	jsr	-$126(a6)	;FindTask
	move.l	d0,16(a3)	;mp_SigTask = myTask
	moveq	#0,d0
	movem.l (a7)+,a3/a6
	rts
.initerr:
	moveq	#$01,d0
	movem.l (a7)+,a3/a6
	rts

.sendpacket:
	bsr	.initpacket	;msg&packet init
	move.l	.devproc,a0
	lea	mymsg,a1
	move.l	4,a6
	jsr	-$16e(a6)	;PutMsg
	lea	myport,a0
	move.l	a0,-(sp)
	jsr	-$180(a6)	;WaitPort
	move.l	(sp)+,a0
	jmp	-$174(a6)	;GetMsg


;	d0 = dp_arg1
.initpacket:
	lea	mymsg,a0
	move.b	#5,8(a0)	;ln_Type  = NT_MESSAGE
	move.l	#myport,14(a0)	;mn_ReplyPort
	lea	mypacket,a1
	move.l	a1,10(a0)	;ln_Name
	move.l	a0,(a1) 	;dp_Link (link to msg)
	move.l	#myport,4(a1)	;dp_Port
	move.l	#31,8(a1)	;dp_Type
	move.l	d0,20(a1)	;dp_Arg1
	rts

.dosname:	dc.b	"dos.library",0
.devname:	dc.b	"DF0:",0
.portname:	dc.b	"XCopy_Diskchange",0
	CNOP 0,4
.dosbase: 	dc.l 0
.devproc:	dc.l 0
.signal: 	dc.b 0
.drive:		DC.B 0

	BSSSEG
	CNOP 0,4
mymsg:		DS.B 14
	CNOP 0,4
mypacket:	DS.B 48
	CNOP 0,4
myport:		DS.B 34
	CODESEG


;	*** OPEN TRACKDISK DEVICE
;	*** d0: drive
;	***----------------------
OpenTrackdisk:
	lea	.drive(pc),a0
	move.l	d0,(a0)
	;
	sub.l	a1,a1
	move.l	4,a6
	jsr	_FindTask(a6)
	lea	myreplyPort,a1
	move.l	d0,16(a1)
	jsr	_AddPort(a6)
	;
	lea	mydiskReq,a1
	move.l	#myreplyPort,14(a1)	;port eintragen
	;
	move.l	.drive(pc),d0		;welches laufwerk
	moveq	#0,d1
	lea	_TDName(PC),a0		;devicename
	move.l	4,a6
	jsr	_OpenDevice(a6)		;trackdisk device oeffnen
	tst.l	d0			;fehler	?
	beq.s	.ok
	bsr.s	RemovePorts
.noreq	moveq	#1,d0
.ok	tst.l	d0
	rts
	;
.drive	DC.L 0

CloseTrackdisk:
	bsr.s	RemovePorts
	lea	mydiskReq,a1		;ioRequest
	jmp	_CloseDevice(a6)		;trackdisk schliessen
RemovePorts:
	lea	myreplyPort,a1		;msgPort
	move.l	4,a6
	jsr	_RemPort(a6)		;entfernen
	rts

_TDName	DC.B "trackdisk.device",0
	EVEN

	BSSSEG
myreplyPort:	DS.B 34
mydiskReq:	DS.B 56

ondrives	DS.B 4		;4 bytes fuer eingeschaltete drives (trackdisk)
drivetracks	DS.W 4		;cyl positionen aus td struktur

 ENDC ;* IFEQ SOLO *
