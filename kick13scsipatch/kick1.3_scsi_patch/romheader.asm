
 ;-- Example Rom Header asm for Remus.

Version = 34
Revision= 5

 DATA


 ;-- Only this part will be included in the ROM.
RomStart:
  dc.w $1114    ;-- ROM Identifier
  dc.w $4ef9    ; 'JMP'
  dc.l $FC00D2  ;-- Jumps to the start of the normal kickstart
                ;-- ..which is a JMP <start address>

;-- this isn't really needed...
  dc.w 0
  dc.w $FFFF

  dc.w Version      ; Rom version
  dc.w Revision     ; Rom revision .. 34.5
RomEnd:

End
