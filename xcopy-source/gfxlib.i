
RecFill	macro
	move.w #\1,D0	;; X1
	move.w #\2,D1	;; y1
	move.w #\3,D2	;; X2
	move.w #\4,D3	;; Y2
	move.b #\5,D4	;; Color
	bsr x_RectFill
	endm

Line	macro
	move.w #\1,D0	;; X1
	move.w #\2,D1	;; y1
	move.w #\3,D2	;; X2
	move.w #\4,D3	;; Y2
	move.b #\5,D4	;; Color
	bsr x_Line
	endm

Plot	macro
	move.w #\1,D0	;; X1
	move.w #\2,D1	;; y1
	move.b #\3,D2	;; Color
	bsr x_Plot
	endm

Box	macro
	move.w #\1,D0	;; X1
	move.w #\2,D1	;; y1
	move.w #\3,D2	;; X2
	move.w #\4,D3	;; Y2
	move.b #\5,D4	;; Color
	bsr x_Box
	endm

SaveWindow	macro
	move.w #\1,D0	;; X1
	move.w #\2,D1	;; y1
	move.w #\3,D2	;; X2
	move.w #\4,D3	;; Y2
	bsr x_SaveWindow
	endm

RestoreWindow macro
	move.l \1,A0
	bsr x_RestoreWindow
	endm

; ** momentan gueltige Flags fuer SetTextMode

SHADOW	= 1
NOSHADOW	= $81
BACKGND	= 2
NOBACKGND	= $82

SetTextMode MACRO
	move.w #\1,D0
	bsr x_SetTextMode
	ENDM

Print	MACRO
	move.l \1,A0	;; Pointer auf Text
	move.w #\2,D0	;; X
	move.w #\3,D1	;; Y
	move.b #\4,D2	;; Farbe
	bsr x_Print
	ENDM

; ** Definitionen fuer die Window-Struktur
WSIZE	= 22	; Bytes

WIN_X1	= 0
WIN_COL	= 8
WIN_BCOL = 9
WIN_STR	= 10
WIN_BOX	= 14
WIN_TXT	= 18

WINDOW	macro

	ifc '','\9'
	FAIL
	mexit
	endc

	dc.w \1,\2,\1+\3,\2+\4	;; X,Y,DX,DY
 IFNE SOLO
	DC.B 0,1
 ELSE
	dc.b \5,\6	;; Hintergrund-/Rahmenfarbe 
 ENDC
	dc.l \7,\8,\9	;; STR,BOX,TXT-Gadget Pointer

WORGX	set \1
WORGY	set \2
	endm

; ** Definitionen fuer die Stringgadget-Struktur

GAD_LEN	= 0
GAD_X	= 2
GAD_Y	= 4
GAD_COL	= 6
GAD_RET	= 7
GAD_STR	= 8

STR_GADGET	macro

	ifc '','\5'
	FAIL
	mexit
	endc

\@0	dc.w \@1-\@0	;; Laenge der Structure
	dc.w WORGX+\1,WORGY+\2	;; xoffset,yoffset
	dc.b \3,\4	;; Color, Rueckgabewert bei Ausloesung
	dc.b \5,0	;; String-Text	
	EVEN
\@1
	endm

; ** Definitionen fuer die Text-Struktur

TXT_LEN	= 0
TXT_X	= 2
TXT_Y	= 4
TXT_COL	= 6
TXT_FLG	= 7	; DrawFlags
TXT_STR	= 8

TXT_GADGET	MACRO
	IFC '','\5'
	FAIL
	MEXIT
	ENDC

\@0	DC.W \@1-\@0		; Laenge der Structure
	DC.W WORGX+\1,WORGY+\2	; xoffset,yoffset
 IFNE SOLO
	DC.B 1,\4
 ELSE
	DC.B \3,\4		; Color, DrawFlags
 ENDC
	DC.B \5,0		; String-Text	
	EVEN
\@1
	ENDM

; ** Definitionen fuer die Boxgadget-Struktur

BOX_LEN	= 0
BOX_X1	= 2
BOX_RET	= 11

BOX_GADGET	MACRO
	IFC '','\8'
	FAIL
	MEXIT
	ENDC

\@0	DC.W \@1-\@0			; Laenge der Structure
	DC.W WORGX+\1,WORGY+\2		; X1,Y1
	DC.W WORGX+\1+\3,WORGY+\2+\4 	; X2,Y2
	DC.B \5,\6			; BoxColor, Rueckgabewert
	DC.B \7	;; TextColor
	DC.B \8,0			; String-Text	
	EVEN
\@1
	endm

