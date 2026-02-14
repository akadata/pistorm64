**
** Sample autoboot code fragment
**
** These are the calling conventions for the Diag routine
**
** A7 -- points to at least 2K of stack
** A6 -- ExecBase
** A5 -- ExpansionBase
** A3 -- your board's ConfigDev structure
** A2 -- Base of diag/init area that was copied
** A0 -- Base of your board
**
** Your Diag routine should return a non-zero value in D0 for success.
** If this value is NULL, then the diag/init area that was copied
** will be returned to the free memory pool.
**

    INCLUDE "exec/types.i"
    INCLUDE "exec/nodes.i"
    INCLUDE "exec/resident.i"
    INCLUDE "libraries/configvars.i"
    INCLUDE "libraries/expansionbase.i"
    INCLUDE "resources/filesysres.i"

    ; LVO offsets are defined locally via EQU below.

ROMINFO     EQU      0
ROMOFFS     EQU     $4000

* ROMINFO defines whether you want the AUTOCONFIG information in
* the beginning of your ROM (set to 0 if you instead have PALS
* providing the AUTOCONFIG information instead)
*
* ROMOFFS is the offset from your board base where your ROMs appear.
* Your ROMs might appear at offset 0 and contain your AUTOCONFIG
* information in the high nibbles of the first $40 words ($80 bytes).
* Or, your autoconfig ID information may be in a PAL, with your
* ROMs possibly being addressed at some offset (for example $2000)
* from your board base.  This ROMOFFS constant will be used as an
* additional offset from your configured board address when patching
* structures which require absolute pointers to ROM code or data.

*----- We'll store Version and Revision in serial number
VERSION 	    EQU	1		; also the high word of serial number
REVISION	    EQU	1		; also the low word of serial number

* See the Addison-Wesley Amiga Hardware Manual for more info.
    
MANUF_ID	    EQU	2011		; CBM assigned (2011 for hackers only)
PRODUCT_ID	    EQU	18		; Manufacturer picks product ID

BOARDSIZE	    EQU	$10000          ; How much address space board decodes
SIZE_FLAG	    EQU	3		; Autoconfig 3-bit flag for BOARDSIZE
			    		;   0=$800000(8meg)  4=$80000(512K)
			    		;   1=$10000(64K)    5=$100000(1meg)
			    		;   2=$20000(128K)   6=$200000(2meg)
			    		;   3=$40000(256K)   7=$400000(4meg)
                CODE

; Exec stuff
AllocMem        EQU -198
InitResident    EQU -102
FindResident    EQU -96
OpenLibrary     EQU -552
CloseLibrary    EQU -414
OpenResource    EQU -$1F2
AddResource     EQU -$1E6
Enqueue         EQU -$10E
AddMemList      EQU -$26A

; Expansion stuff
MakeDosNode     EQU -144
AddDosNode      EQU -150
AddBootNode     EQU -36
FindConfigDev   EQU -72

; PiSCSI64 MMIO window:
;   board base is provided in A0 / cd_BoardAddr(a3)
;   register block lives at board_base + PISCSI64_REG_BASE
PISCSI64_REG_BASE EQU $00000000
PISCSI64_MMIO_BASE EQU $00E90000
PiSCSI64Addr1     EQU $0010
PiSCSI64Addr2     EQU $0014
PiSCSI64Addr3     EQU $0018
PiSCSI64Addr4     EQU $001C
PiSCSI64DebugMe   EQU $0020
PiSCSI64Driver    EQU $0040
PiSCSI64NextPart  EQU $0044
PiSCSI64GetPart   EQU $0048
PiSCSI64GetPrio   EQU $004C
PiSCSI64GetFS     EQU $0060
PiSCSI64NextFS    EQU $0064
PiSCSI64CopyFS    EQU $0068
PiSCSI64FSSize    EQU $006C
PiSCSI64SetFSH    EQU $0070
PiSCSI64LoadFS    EQU $0084
PiSCSI64GetFSInfo EQU $0088
PiSCSI64Dbg1      EQU $1010
PiSCSI64Dbg2      EQU $1014
PiSCSI64Dbg3      EQU $1018
PiSCSI64Dbg4      EQU $101C
PiSCSI64Dbg5      EQU $1020
PiSCSI64Dbg6      EQU $1024
PiSCSI64Dbg7      EQU $1028
PiSCSI64Dbg8      EQU $102C
PiSCSI64DbgMsg    EQU $1000

*******  RomStart  ***************************************************
**********************************************************************

RomStart:

*******  DiagStart  **************************************************
DiagStart:  ; This is the DiagArea structure whose relative offset from
            ; your board base appears as the Init Diag vector in your
            ; autoconfig ID information.  This structure is designed
            ; to use all relative pointers (no patching needed).
            dc.b    DAC_WORDWIDE+DAC_CONFIGTIME    ; da_Config
            dc.b    0                              ; da_Flags
            dc.w    EndCopy-DiagStart              ; da_Size
            dc.w    DiagEntry-DiagStart            ; da_DiagPoint
            dc.w    BootEntry-DiagStart            ; da_BootPoint
            dc.w    DevName-DiagStart              ; da_Name
            dc.w    0                              ; da_Reserved01
            dc.w    0                              ; da_Reserved02

*******  Resident Structure  *****************************************
Romtag:
            dc.w    RTC_MATCHWORD      ; UWORD RT_MATCHWORD
rt_Match:   dc.l    Romtag-DiagStart   ; APTR  RT_MATCHTAG
rt_End:     dc.l    EndCopy-DiagStart  ; APTR  RT_ENDSKIP
            dc.b    RTW_COLDSTART      ; UBYTE RT_FLAGS
            dc.b    VERSION            ; UBYTE RT_VERSION
            dc.b    NT_DEVICE          ; UBYTE RT_TYPE
            dc.b    20                 ; BYTE  RT_PRI
rt_Name:    dc.l    DevName-DiagStart  ; APTR  RT_NAME
rt_Id:      dc.l    IdString-DiagStart ; APTR  RT_IDSTRING
rt_Init:    dc.l    Init-RomStart      ; APTR  RT_INIT


******* Strings referenced in Diag Copy area  ************************
DevName:    dc.b    'pi-scsi64.device',0,0                      ; Name string
IdString    dc.b    'PiSCSI64 ROM 44.0',0   ; Id string

DosName:        dc.b    'dos.library',0                ; DOS library name
ExpansionName:  dc.b    "expansion.library",0
LibName:        dc.b    "pi-scsi64.device",0,0

DosDevName: dc.b    'ABC',0        ; dos device name for MakeDosNode()
                                   ;   (dos device will be ABC:)

            ds.w    0              ; word align

*******  DiagEntry  **************************************************
**********************************************************************
*
*   success = DiagEntry(BoardBase,DiagCopy, configDev)
*   d0                  a0         a2                  a3
*
*   Called by expansion architecture to relocate any pointers
*   in the copied diagnostic area.   We will patch the romtag.
*   If you have pre-coded your MakeDosNode packet, BootNode,
*   or device initialization structures, they would also need
*   to be within this copy area, and patched by this routine.
*
**********************************************************************

DiagEntry:
            align 2
            nop
            nop
            nop
            movea.l a0,a5
            adda.l #PISCSI64_REG_BASE,a5
            move.l #1,PiSCSI64DebugMe(a5)
            move.l a3,PiSCSI64Addr1(a5)
            move.l #12,PiSCSI64DebugMe(a5)
            nop
            nop
            nop
            nop
            nop
            nop

            lea      patchTable-RomStart(a0),a1   ; find patch table
            adda.l   #ROMOFFS,a1                  ; adjusting for ROMOFFS

* Patch relative pointers to labels within DiagCopy area
* by adding Diag RAM copy address.  These pointers were coded as
* long relative offsets from base of the DiagArea structure.
*
dpatches:
            move.l   a2,d1           ;d1=base of ram Diag copy
dloop:
            move.w   (a1)+,d0        ;d0=word offs. into Diag needing patch
            bmi.s    bpatches        ;-1 is end of word patch offset table
            add.l    d1,0(a2,d0.w)   ;add DiagCopy addr to coded rel. offset
            bra.s    dloop

* Patches relative pointers to labels within the ROM by adding
* the board base address + ROMOFFS.  These pointers were coded as
* long relative offsets from RomStart.
*
bpatches:
            move.l #13,PiSCSI64DebugMe(a5)
            move.l   a0,d1           ;d1 = board base address
            add.l    #ROMOFFS,d1     ;add offset to where your ROMs are
rloop:
            move.w   (a1)+,d0        ;d0=word offs. into Diag needing patch
            bmi.s   endpatches       ;-1 is end of patch offset table
            add.l   d1,0(a2,d0.w)    ;add ROM address to coded relative offset
            bra.s   rloop

endpatches:
            move.l #14,PiSCSI64DebugMe(a5)
            moveq.l #1,d0           ; indicate "success"
            rts


*******  BootEntry  **************************************************
**********************************************************************

BootEntry:
            align 2
            movea.l a0,a5
            adda.l #PISCSI64_REG_BASE,a5
            move.l #2,PiSCSI64DebugMe(a5)
            lea DosName(pc),a1
            jsr FindResident(a6)
            tst.l d0
            beq.b .End
            move.l d0,a0
            move.l RT_INIT(a0),a0
            jsr (a0)
.End
            moveq.l #1,d0           ; indicate "success"
            rts

*
* End of the Diag copy area which is copied to RAM
*
EndCopy:
*************************************************************************

*************************************************************************
*
*   Beginning of ROM driver code and data that is accessed only in
*   the ROM space.  This must all be position-independent.
*

patchTable:
* Word offsets into Diag area where pointers need Diag copy address added
            dc.w   rt_Match-DiagStart
            dc.w   rt_End-DiagStart
            dc.w   rt_Name-DiagStart
            dc.w   rt_Id-DiagStart
            dc.w   -1

* Word offsets into Diag area where pointers need boardbase+ROMOFFS added
            dc.w   rt_Init-DiagStart
            dc.w   -1

*******  Romtag InitEntry  **********************************************
*************************************************************************

Init:       ; After Diag patching, our romtag will point to this
            ; routine in ROM so that it can be called at Resident
            ; initialization time.
            ; This routine will be similar to a normal expansion device
            ; initialization routine, but will MakeDosNode then set up a
            ; BootNode, and Enqueue() on eb_MountList.
            ;
            align 2
            movem.l d1-d7/a0-a6,-(sp)
            ;move.w #$00B8,$dff09a       ; Disable interrupts during init
            ; InitResident does not guarantee A3. Resolve board base via FindConfigDev.
            movea.l 4,a6
            lea ExpansionName(pc),a1
            moveq #0,d0
            jsr OpenLibrary(a6)
            move.l d0,a4
            beq.s UseDefaultMMIOBase

            movea.l d0,a6
            suba.l a0,a0
            move.l #MANUF_ID,d0
            move.l #PRODUCT_ID,d1
            jsr FindConfigDev(a6)
            tst.l d0
            beq.s UseDefaultMMIOBaseClose
            movea.l d0,a0
            movea.l cd_BoardAddr(a0),a5
            move.l a5,d2
            tst.l d2
            bne.s CloseMMIOProbeLib

UseDefaultMMIOBaseClose:
            movea.l #PISCSI64_MMIO_BASE,a5

CloseMMIOProbeLib:
            movea.l 4,a6
            movea.l a4,a1
            jsr CloseLibrary(a6)
            bra.s HaveMMIOBase

UseDefaultMMIOBase:
            movea.l #PISCSI64_MMIO_BASE,a5

HaveMMIOBase:
            adda.l #PISCSI64_REG_BASE,a5
            move.l  #3,PiSCSI64DebugMe(a5)
            move.l a3,PiSCSI64Addr4(a5)

            movea.l 4,a6

NoZ3:
            move.l  #11,PiSCSI64DebugMe(a5)
            lea LibName(pc),a1
            jsr FindResident(a6)
            move.l  #10,PiSCSI64DebugMe(a5)
            cmp.l #0,d0
            bne.s SkipDriverLoad        ; Library is already loaded, jump straight to partitions

            move.l  #4,PiSCSI64DebugMe(a5)
            movea.l 4,a6
            move.l #$40000,d0
            moveq #0,d1
            jsr AllocMem(a6)            ; Allocate memory for the PiStorm to copy the driver to

            move.l  d0,PiSCSI64Driver(a5)     ; Copy the PiSCSI64 driver to allocated memory and patch offsets

            move.l  #5,PiSCSI64DebugMe(a5)
            move.l  d0,a1
            move.l  #0,d1
            movea.l  4,a6
            move.w  #$17FF,d2            ; scan up to 0x3000 bytes (driver payload) in words
FindDriverRomTag:
            cmp.w   #RTC_MATCHWORD,(a1)
            beq.s   HaveDriverRomTag
            addq.l  #2,a1
            dbf     d2,FindDriverRomTag
            move.l  d0,a1
            add.l   #$028,a1             ; fallback legacy offset
            move.l  #16,PiSCSI64DebugMe(a5)
            bra.s   DoInitResident
HaveDriverRomTag:
            move.l  #15,PiSCSI64DebugMe(a5)
DoInitResident:
            jsr InitResident(a6)        ; Initialize the PiSCSI64 driver

SkipDriverLoad:
            move.l  #9,PiSCSI64DebugMe(a5)
            jsr LoadFileSystems(pc)

FSLoadExit:
            lea ExpansionName(pc),a1
            moveq #0,d0
            jsr OpenLibrary(a6)         ; Open expansion.library to make this work, somehow
            move.l a6,a4
            move.l d0,a6

            move.l  #7,PiSCSI64DebugMe(a5)
PartitionLoop:
            move.l PiSCSI64GetPart(a5),d0     ; Get the available partition in the current slot
            beq.w EndPartitions         ; If the next partition returns 0, there's no additional partitions
            move.l d0,a0
            jsr MakeDosNode(a6)
            cmp.l #0,PiSCSI64GetFSInfo(a5)
            beq.s SkipLoadFS

            move.l d0,PiSCSI64LoadFS(a5)        ; Attempt to load the file system driver from data/fs
            cmp.l #$FFFFFFFF,PiSCSI64Addr4(a5)
            beq SkipLoadFS

            jsr LoadFileSystems(pc)

SkipLoadFS:
            move.l d0,PiSCSI64SetFSH(a5)
            move.l d0,a0
            move.l PiSCSI64GetPrio(a5),d0
            move.l #0,d1
            move.l PiSCSI64Addr1(a5),a1

* BOOL AddBootNode( LONG bootPri, ULONG flags, struct DeviceNode *deviceNode, struct ConfigDev *configDev );
* amicall(ExpansionBase, 0x24, AddBootNode(d0,d1,a0,a1))
            move.l #38,PiSCSI64DebugMe(a5)
            jsr AddBootNode(a6)
            move.l #1,PiSCSI64NextPart(a5)    ; Switch to the next partition
            bra.w PartitionLoop


EndPartitions:
            move.l #8,PiSCSI64DebugMe(a5)
            move.l a6,a1
            move.l #800,PiSCSI64DebugMe(a5)
            movea.l 4,a6
            move.l #801,PiSCSI64DebugMe(a5)
            jsr CloseLibrary(a6)
            move.l #802,PiSCSI64DebugMe(a5)

            move.l #803,PiSCSI64DebugMe(a5)

            ;move.w #$80B8,$dff09a       ; Re-enable interrupts
            move.l #804,PiSCSI64DebugMe(a5)
            moveq.l #1,d0               ; indicate "success"
            move.l #805,PiSCSI64DebugMe(a5)
            movem.l (sp)+,d1-d7/a0-a6
            rts

            align 4
FileSysName     dc.b    'FileSystem.resource',0
FileSysCreator  dc.b    'PiStorm',0

CurFS:          dc.l    $0
FSResource:     dc.l    $0

            align 2
LoadFileSystems:
            movem.l d0-d7/a0-a6,-(sp)       ; Push registers to stack
            move.l #30,PiSCSI64DebugMe(a5)
            movea.l 4,a6

FSNext:
            move.l PiSCSI64GetFS(a5),d0
            cmp.l #0,d0
            beq.s FSDone

            move.l #39,PiSCSI64DebugMe(a5)
            move.l PiSCSI64FSSize(a5),d0
            move.l #40,PiSCSI64DebugMe(a5)
            move.l #$10001,d1
            move.l #41,PiSCSI64DebugMe(a5)
            jsr AllocMem(a6)
            tst.l d0
            beq.s FSAdvance

            move.l d0,PiSCSI64Addr3(a5)
            move.l d0,a0
            move.l #1,PiSCSI64CopyFS(a5)

FSAdvance:
            move.l #480,PiSCSI64DebugMe(a5)
            move.l #1,PiSCSI64NextFS(a5)
            bra.s FSNext

FSDone:
            move.l #32,PiSCSI64DebugMe(a5)
            movem.l (sp)+,d0-d7/a0-a6   ; Pop registers from stack
            rts

FSRes
    dc.l    0
    dc.l    0
    dc.b    NT_RESOURCE
    dc.b    0
    dc.l    FileSysName
    dc.l    FileSysCreator
.Head
    dc.l    .Tail
.Tail
    dc.l    0
    dc.l    .Head
    dc.b    NT_RESOURCE
    dc.b    0
