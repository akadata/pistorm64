#!/usr/bin/env python3
"""
Opcode metadata generator for the AArch64 JIT.

Reads opcode information from ProcessorTests JSON and generates
jit_68000_opinfo.h and jit_68000_opinfo.c with metadata for all 65536 opcodes.

Each entry includes:
- handler family
- size kind
- extension word count
- source EA class
- destination EA class
- reads CCR
- writes CCR
- privileged
- may trap
- block end
"""

import json
import os
import re
import sys

# EA (Effective Address) classes
EA_NONE = 0           # No EA
EA_DN = 1             # Data register direct
EA_AN = 2             # Address register direct
EA_AI = 3             # Address register indirect
EA_PI = 4             # Address register indirect with postincrement
EA_PD = 5             # Address register indirect with predecrement
EA_DI = 6             # Address register indirect with displacement
EA_IX = 7             # Address register indirect with index
EA_AW = 8             # Absolute word
EA_AL = 9             # Absolute long
EA_PCDI = 10          # PC relative with displacement
EA_PCIX = 11          # PC relative with index
EA_IMM = 12           # Immediate

# Size kinds
SIZE_NONE = 0
SIZE_BYTE = 1
SIZE_WORD = 2
SIZE_LONG = 3

# Handler families
FAMILY_ILLEGAL = 0
FAMILY_MOVE = 1
FAMILY_MOVEQ = 2
FAMILY_MOVEP = 3
FAMILY_MOVEM = 4
FAMILY_MOVE16 = 5
FAMILY_ADD = 6
FAMILY_ADDQ = 7
FAMILY_ADDX = 8
FAMILY_SUB = 9
FAMILY_SUBQ = 10
FAMILY_SUBX = 11
FAMILY_CMP = 12
FAMILY_CMPM = 13
FAMILY_AND = 14
FAMILY_OR = 15
FAMILY_EOR = 16
FAMILY_ABCD = 17
FAMILY_NEG = 18
FAMILY_NEGX = 19
FAMILY_NOT = 20
FAMILY_CLR = 21
FAMILY_TST = 22
FAMILY_TAS = 23
FAMILY_BTST = 24
FAMILY_BSET = 25
FAMILY_BCLR = 26
FAMILY_BCHG = 27
FAMILY_SCC = 28
FAMILY_DBCC = 29
FAMILY_BCC = 30
FAMILY_BSR = 31
FAMILY_BRA = 32
FAMILY_JMP = 33
FAMILY_JSR = 34
FAMILY_RTS = 35
FAMILY_RTR = 36
FAMILY_RTE = 37
FAMILY_RTD = 38
FAMILY_LINK = 39
FAMILY_UNLK = 40
FAMILY_RESET = 41
FAMILY_NOP = 42
FAMILY_STOP = 43
FAMILY_ANDI = 44
FAMILY_ORI = 45
FAMILY_EORI = 46
FAMILY_CMPI = 47
FAMILY_SUBI = 48
FAMILY_ADDI = 49
FAMILY_MULU = 50
FAMILY_MULS = 51
FAMILY_DIVU = 52
FAMILY_DIVS = 53
FAMILY_ASL = 54
FAMILY_ASR = 55
FAMILY_LSL = 56
FAMILY_LSR = 57
FAMILY_ROL = 58
FAMILY_ROR = 59
FAMILY_ROXL = 60
FAMILY_ROXR = 61
FAMILY_SWAP = 62
FAMILY_PEA = 63
FAMILY_EXT = 64
FAMILY_EXTB = 65
FAMILY_LEA = 66
FAMILY_CHK = 67
FAMILY_LINE_A = 68
FAMILY_LINE_F = 69
FAMILY_TRAP = 70
FAMILY_TRAPV = 71
FAMILY_ILL = 72
FAMILY_ORI_CCR = 73
FAMILY_ANDI_CCR = 74
FAMILY_EORI_CCR = 75
FAMILY_ORI_SR = 76
FAMILY_ANDI_SR = 77
FAMILY_EORI_SR = 78
FAMILY_MOVE_CCR = 79
FAMILY_MOVE_SR = 80
FAMILY_MOVE_USP = 81
FAMILY_MOVEC = 82
FAMILY_BKPT = 83
FAMILY_CALLM = 84
FAMILY_RTM = 85
FAMILY_TRAPCC = 86
FAMILY_CP = 87      # Coprocessor
FAMILY_CAS = 88
FAMILY_CAS2 = 89
FAMILY_CHK2 = 90
FAMILY_CMP2 = 91
FAMILY_DIVL = 92
FAMILY_MULL = 93
FAMILY_PACK = 94
FAMILY_UNPK = 95
FAMILY_BF = 96      # Bitfield ops
FAMILY_MOVE16 = 97

# Family name mapping (for validation and debugging)
FAMILY_NAMES = {v: k for k, v in list(globals().items()) if k.startswith('FAMILY_')}

# CCR flags
CCR_X = 0x10
CCR_N = 0x08
CCR_Z = 0x04
CCR_V = 0x02
CCR_C = 0x01
CCR_ALL = CCR_X | CCR_N | CCR_Z | CCR_V | CCR_C


def get_family_from_opcode(opcode, desc):
    """
    Determine instruction family from opcode bit pattern.
    This is more reliable than parsing description strings.
    
    Based on Musashi's opcode table in m68k_in.c
    """
    desc_upper = desc.upper() if desc != "None" else ""
    
    # LINE_A: 0xA000-0xAFFF
    if 0xA000 <= opcode <= 0xAFFF:
        return FAMILY_LINE_A
    
    # LINE_F: 0xF000-0xFFFF
    if 0xF000 <= opcode <= 0xFFFF:
        return FAMILY_LINE_F
    
    # === SPECIFIC PATTERNS MUST COME BEFORE GENERAL ONES ===
    
    # MOVE to CCR: 0100010011000000 = 0x44C0 (before general MOVE)
    if (opcode & 0xFFF8) == 0x44C0:
        return FAMILY_MOVE_CCR
    
    # MOVE from SR: 0100000011000000 = 0x40C0 (before general MOVE)
    if (opcode & 0xFFF8) == 0x40C0:
        return FAMILY_MOVE_SR
    
    # MOVE to USP: 0100111001100000/0100111001101000 = 0x4E60/0x4E68
    if (opcode & 0xFFF8) == 0x4E60 or (opcode & 0xFFF8) == 0x4E68:
        return FAMILY_MOVE_USP
    
    # MOVEC: 0x4E7A/0x4E7B (68010+)
    if opcode == 0x4E7A or opcode == 0x4E7B:
        return FAMILY_MOVEC
    
    # MOVEP: 00001101........ and 00001100........ (before general MOVE)
    if (opcode & 0xF100) == 0x0100 or (opcode & 0xF100) == 0x0180:
        return FAMILY_MOVEP
    
    # MOVE: 0x1000-0x1FFF, 0x2000-0x3FFF (general case, after specific MOVEs)
    if 0x1000 <= opcode <= 0x3FFF:
        return FAMILY_MOVE
    
    # MOVEQ: 0111...0........ = 0x7000-0x70FF
    if 0x7000 <= opcode <= 0x70FF:
        return FAMILY_MOVEQ
    
    # Bcc: 0110.... ........ = 0x6000-0x60FF
    # BRA: 01100000........ = 0x6000-0x60FF
    if (opcode & 0xFF00) == 0x6000:
        if (opcode & 0x00FF) == 0:  # BRA.W or BRA.L
            return FAMILY_BRA
        cond = (opcode >> 8) & 0xF
        if cond == 0:
            return FAMILY_BRA
        else:
            return FAMILY_BCC
    
    # BSR: 01100001........ = 0x6100-0x61FF
    if (opcode & 0xFF00) == 0x6100:
        return FAMILY_BSR
    
    # SCC: 0101 cccc 11...... = 0x50C0-0x5FC0 (bits 6-7 = 11)
    # Check BEFORE ADDQ/SUBQ to avoid overlap
    if (opcode & 0xF0C0) == 0x50C0:
        return FAMILY_SCC
    
    # DBcc: 01010101...... = 0x54C8-0x54FF
    if (opcode & 0xFFF8) == 0x54C8:
        return FAMILY_DBCC
    
    # ADDQ: 0101...000...... = 0x5000-0x50FF (bit 11 = 0, bit 8 = 0)
    # Note: SCC (bits 6-7=11) already checked above
    if (opcode & 0xF100) == 0x5000:
        return FAMILY_ADDQ

    # ADDI: 00000110........ = 0x0600-0x06FF
    if (opcode & 0xFF00) == 0x0600:
        return FAMILY_ADDI

    # SUB: 1001...0........ and 1001...1........
    if (opcode & 0xF100) == 0x9000 or (opcode & 0xF100) == 0x9100:
        return FAMILY_SUB

    # SUBQ: 0101...100...... = 0x5100-0x51FF (bit 11 = 1, bit 8 = 0)
    if (opcode & 0xF100) == 0x5100:
        return FAMILY_SUBQ
    
    # SUBI: 00000100........ = 0x0400-0x04FF
    if (opcode & 0xFF00) == 0x0400:
        return FAMILY_SUBI
    
    # CMP: 1011...0........ and 1011...1........
    if (opcode & 0xF100) == 0xB000 or (opcode & 0xF100) == 0xB100:
        # CMPM: 1011...100001... = 0xB108-0xB13F
        if (opcode & 0xF138) == 0xB108:
            return FAMILY_CMPM
        # CMPI: 00001100........ = 0x0C00-0x0CFF
        if (opcode & 0xFF00) == 0x0C00:
            return FAMILY_CMPI
        return FAMILY_CMP
    
    # CMPM: 1011...100001... = 0xB108-0xB13F
    if (opcode & 0xF138) == 0xB108:
        return FAMILY_CMPM
    
    # CMPI: 00001100........ = 0x0C00-0x0CFF
    if (opcode & 0xFF00) == 0x0C00:
        return FAMILY_CMPI
    
    # AND: 1100...0........ and 1100...1........
    if (opcode & 0xF100) == 0xC000 or (opcode & 0xF100) == 0xC100:
        return FAMILY_AND
    
    # ANDI: 00000010........ = 0x0200-0x02FF
    if (opcode & 0xFF00) == 0x0200:
        # ANDI to CCR: 0000001000111100 = 0x023C
        if opcode == 0x023C:
            return FAMILY_ANDI_CCR
        # ANDI to SR: 0000001001111100 = 0x027C
        if opcode == 0x027C:
            return FAMILY_ANDI_SR
        return FAMILY_ANDI
    
    # OR: 1000...0........ and 1000...1........
    if (opcode & 0xF100) == 0x8000 or (opcode & 0xF100) == 0x8100:
        return FAMILY_OR
    
    # ORI: 00000000........ = 0x0000-0x00FF
    if (opcode & 0xFF00) == 0x0000:
        # ORI to CCR: 0000000000111100 = 0x003C
        if opcode == 0x003C:
            return FAMILY_ORI_CCR
        # ORI to SR: 0000000001111100 = 0x007C
        if opcode == 0x007C:
            return FAMILY_ORI_SR
        return FAMILY_ORI
    
    # EOR: 1011...0........ with specific pattern
    if (opcode & 0xF100) == 0xB000:
        # EOR: 1011...1..000... = 0xB100-0xB107, 0xB300-0xB307, etc.
        if (opcode & 0x0108) == 0x0100:
            return FAMILY_EOR
    
    # EORI: 00001010........ = 0x0A00-0x0AFF
    if (opcode & 0xFF00) == 0x0A00:
        # EORI to CCR: 0000101000111100 = 0x0A3C
        if opcode == 0x0A3C:
            return FAMILY_EORI_CCR
        # EORI to SR: 0000101001111100 = 0x0A7C
        if opcode == 0x0A7C:
            return FAMILY_EORI_SR
        return FAMILY_EORI
    
    # ABCD: 1100...100000... and 1100...100001...
    if (opcode & 0xF1F0) == 0xC100:
        return FAMILY_ABCD
    
    # NEG: 01000100........ = 0x4400-0x44FF
    if (opcode & 0xFF00) == 0x4400:
        return FAMILY_NEG
    
    # NEGX: 01000000........ = 0x4000-0x40FF
    if (opcode & 0xFF00) == 0x4000:
        return FAMILY_NEGX
    
    # NOT: 01000110........ = 0x4600-0x46FF
    if (opcode & 0xFF00) == 0x4600:
        return FAMILY_NOT
    
    # CLR: 01000010........ = 0x4200-0x42FF
    if (opcode & 0xFF00) == 0x4200:
        return FAMILY_CLR
    
    # TST: 01001010........ = 0x4A00-0x4AFF
    if (opcode & 0xFF00) == 0x4A00:
        return FAMILY_TST
    
    # TAS: 0100101011...... = 0x4AC0-0x4AFF
    if (opcode & 0xFFC0) == 0x4AC0:
        return FAMILY_TAS
    
    # BTST/BSET/BCLR/BCHG: 0000...1........
    if (opcode & 0xF100) == 0x0100:
        op = (opcode >> 6) & 3
        if op == 0:
            return FAMILY_BTST
        elif op == 1:
            return FAMILY_BSET
        elif op == 2:
            return FAMILY_BCLR
        elif op == 3:
            return FAMILY_BCHG
    
    # DBcc: 01010101...... = 0x54C8-0x54FF (MUST come before SCC)
    if (opcode & 0xFFF8) == 0x54C8:
        return FAMILY_DBCC
    
    # SCC: 0101010011...... = 0x54C0-0x54FF (but not DBcc)
    if (opcode & 0xFFC0) == 0x54C0:
        return FAMILY_SCC
    
    # JMP: 0100111010...... = 0x4EC0-0x4ECF
    if (opcode & 0xFFC0) == 0x4EC0:
        return FAMILY_JMP
    
    # JSR: 0100111011...... = 0x4ED0-0x4EDF
    if (opcode & 0xFFC0) == 0x4ED0:
        return FAMILY_JSR
    
    # TRAPV: 0100111001110110 = 0x4E76
    if opcode == 0x4E76:
        return FAMILY_TRAPV
    
    # RTS: 0100111001110100 = 0x4E74
    if opcode == 0x4E74:
        return FAMILY_RTS
    
    # RTD: 0100111001110101 = 0x4E75
    if opcode == 0x4E75:
        return FAMILY_RTD
    
    # RTR: 0100111001110111 = 0x4E77
    if opcode == 0x4E77:
        return FAMILY_RTR
    
    # RTE: 0100111001110011 = 0x4E73
    if opcode == 0x4E73:
        return FAMILY_RTE
    
    # RESET: 0100111001110000 = 0x4E70
    if opcode == 0x4E70:
        return FAMILY_RESET
    
    # NOP: 0100111001110001 = 0x4E71
    if opcode == 0x4E71:
        return FAMILY_NOP
    
    # STOP: 0100111001110010 = 0x4E72
    if opcode == 0x4E72:
        return FAMILY_STOP
    
    # UNLK: 0100111001011xxx = 0x4E58-0x4E5F (register in bits 0-2)
    if (opcode & 0xFFF8) == 0x4E58:
        return FAMILY_UNLK
    
    # LINK: 0100111001010000 = 0x4E50 (LINK An, #disp16)
    if (opcode & 0xFFF8) == 0x4E50:
        return FAMILY_LINK
    
    # MOVE to USP: 0100111001100000/0100111001101000 = 0x4E60/0x4E68
    if (opcode & 0xFFF8) == 0x4E60 or (opcode & 0xFFF8) == 0x4E68:
        return FAMILY_MOVE_USP
    
    # MOVEC: 0100111001111010/0100111001111011 = 0x4E7A/0x4E7B (68010+)
    if opcode == 0x4E7A or opcode == 0x4E7B:
        return FAMILY_MOVEC
    
    # TRAP: 010011100100.... = 0x4E40-0x4E4F
    if (opcode & 0xFFF0) == 0x4E40:
        return FAMILY_TRAP
    
    # JSR: 0100111011...... = 0x4ED0-0x4EDF (MUST come before JMP)
    if (opcode & 0xFFC0) == 0x4ED0:
        return FAMILY_JSR
    
    # JMP: 0100111010...... = 0x4EC0-0x4ECF
    if (opcode & 0xFFC0) == 0x4EC0:
        return FAMILY_JMP
    
    # JSR: 0100111011...... = 0x4ED0-0x4EDF
    if (opcode & 0xFFC0) == 0x4ED0:
        return FAMILY_JSR
    
    # SWAP: 0100100001000... = 0x4840-0x4847 (must come before PEA)
    if (opcode & 0xFFF8) == 0x4840:
        return FAMILY_SWAP
    
    # PEA: 0100100001...... = 0x4840-0x487F (but not SWAP)
    if (opcode & 0xFFC0) == 0x4840:
        return FAMILY_PEA
    
    # TAS: 0100101011...... = 0x4AC0-0x4AFF (must come before TST)
    if (opcode & 0xFFC0) == 0x4AC0:
        return FAMILY_TAS
    
    # EXT: 0100100010...... = 0x4880-0x48BF
    if (opcode & 0xFFC0) == 0x4880:
        return FAMILY_EXT
    
    # EXTB: 0100100011000... = 0x49C0-0x49C7 (68020+)
    if (opcode & 0xFFF8) == 0x49C0:
        return FAMILY_EXTB
    
    # MOVEM: 0100100011...... and 0100110011......
    if (opcode & 0xF180) == 0x4880 or (opcode & 0xF180) == 0x4900:
        return FAMILY_MOVEM
    
    # MOVEP: 00001101........ and 00001100........ (0x0D00-0x0DFF, 0x0C00-0x0CFF)
    if (opcode & 0xF100) == 0x0100 or (opcode & 0xF100) == 0x0180:
        return FAMILY_MOVEP
    
    # LEA: 01001111........ = 0x4F00-0x4FFF
    if (opcode & 0xF100) == 0x4F00:
        return FAMILY_LEA
    
    # BKPT: 0100100001001... = 0x4848-0x484F (68010+)
    if (opcode & 0xFFF8) == 0x4848:
        return FAMILY_BKPT
    
    # ILLEGAL: 0100101011111100 = 0x4AFC
    if opcode == 0x4AFC:
        return FAMILY_ILL
    
    # ABCD: 1100...100000... and 1100...100001... = 0xC100-0xC107, 0xC108-0xC10F
    if (opcode & 0xF1F8) == 0xC100 or (opcode & 0xF1F8) == 0xC108:
        return FAMILY_ABCD
    
    # ADDX: 1101...100000... and 1101...100001... = 0xD100-0xD107, 0xD108-0xD10F
    if (opcode & 0xF1F8) == 0xD100 or (opcode & 0xF1F8) == 0xD108:
        return FAMILY_ADDX
    
    # SUBX: 1001...100000... and 1001...100001... = 0x9100-0x9107, 0x9108-0x910F
    if (opcode & 0xF1F8) == 0x9100 or (opcode & 0xF1F8) == 0x9108:
        return FAMILY_SUBX
    
    # EOR: 1011...1..000... = 0xB100-0xB107, 0xB300-0xB307, etc.
    if (opcode & 0xF138) == 0xB100:
        return FAMILY_EOR
    
    # MULU: 1100...011...... = 0xC000-0xC03F
    if (opcode & 0xF1C0) == 0xC000:
        return FAMILY_MULU
    
    # MULS: 1100...011...... = 0xC040-0xC07F
    if (opcode & 0xF1C0) == 0xC040:
        return FAMILY_MULS
    
    # DIVU: 1000...011...... = 0x8000-0x803F
    if (opcode & 0xF1C0) == 0x8000:
        return FAMILY_DIVU
    
    # DIVS: 1000...011...... = 0x8040-0x807F
    if (opcode & 0xF1C0) == 0x8040:
        return FAMILY_DIVS
    
    # CHK: 01000000........ = 0x4000-0x40FF with specific pattern
    if (opcode & 0xF100) == 0x4000 and (opcode & 0x00C0) == 0x0080:
        return FAMILY_CHK
    
    # SCC: 0101010011...... = 0x54C0-0x54FF
    if (opcode & 0xFFC0) == 0x54C0:
        return FAMILY_SCC
    
    # DBcc: 01010101...... = 0x54C8-0x54FF
    if (opcode & 0xFFF8) == 0x54C8:
        return FAMILY_DBCC
    
    # TAS: 0100101011...... = 0x4AC0-0x4AFF (must come before TST)
    if (opcode & 0xFFC0) == 0x4AC0:
        return FAMILY_TAS
    
    # ASL/ASR/LSL/LSR/ROL/ROR/ROXL/ROXR register: 1110...0/1...0/1...
    # Pattern: 1110...000000... through 1110...110100...
    if (opcode & 0xF000) == 0xE000:
        # Check if it's a register shift (not memory shift)
        if (opcode & 0x0080) == 0:  # Not memory shift (bit 7 = 0)
            shift_type = (opcode >> 3) & 7
            direction = (opcode >> 8) & 1
            reg_shift = (opcode >> 5) & 1  # 0 = immediate, 1 = register
            
            if shift_type == 0:
                return FAMILY_ASR if direction == 0 else FAMILY_ASL
            elif shift_type == 1:
                return FAMILY_LSR if direction == 0 else FAMILY_LSL
            elif shift_type == 2:
                return FAMILY_ROXR if direction == 0 else FAMILY_ROXL
            elif shift_type == 3:
                return FAMILY_ROR if direction == 0 else FAMILY_ROL
    
    # ASR/LSR/etc memory: 111000/1...11......
    if (opcode & 0xFF00) == 0xE000 or (opcode & 0xFF00) == 0xE100:
        if (opcode & 0x00C0) == 0x0080:  # Memory mode
            if (opcode & 0x0100) == 0:
                return FAMILY_ASR
            else:
                return FAMILY_ASL
    
    # Bitfield ops (BF): 111010/1....... (040+) - specific patterns
    # BFCHG: 1110101011......, BFCLR: 1110110011......
    # BFEXTS: 1110101111......, BFEXTU: 1110100111......
    # BFFFO: 1110110111......, BFINS: 1110111111......
    # BFSET: 1110111011......, BFTST: 1110100011......
    bf_pattern = opcode & 0xFF80
    if bf_pattern in (0xEA80, 0xEC80, 0xEB80, 0xE980, 0xED80, 0xEF80, 0xEE80, 0xE880):
        # Only if destination is not a simple register (check EA mode)
        if (opcode & 0x003F) >= 0x0020:  # Memory EA modes
            return FAMILY_BF
    
    # MOVE to CCR: 0100010011000000 = 0x44C0
    if (opcode & 0xFFF8) == 0x44C0:
        return FAMILY_MOVE_CCR
    
    # MOVE from SR: 0100000011000000 = 0x40C0
    if (opcode & 0xFFF8) == 0x40C0:
        return FAMILY_MOVE_SR
    
    # ORI to CCR: 0000000000111100 = 0x003C
    if opcode == 0x003C:
        return FAMILY_ORI_CCR
    
    # ORI to SR: 0000000001111100 = 0x007C
    if opcode == 0x007C:
        return FAMILY_ORI_SR
    
    # ANDI to CCR: 0000001000111100 = 0x023C
    if opcode == 0x023C:
        return FAMILY_ANDI_CCR
    
    # ANDI to SR: 0000001001111100 = 0x027C
    if opcode == 0x027C:
        return FAMILY_ANDI_SR
    
    # EORI to CCR: 0000101000111100 = 0x0A3C
    if opcode == 0x0A3C:
        return FAMILY_EORI_CCR
    
    # EORI to SR: 0000101001111100 = 0x0A7C
    if opcode == 0x0A7C:
        return FAMILY_EORI_SR
    
    # RTS: 0100111001110100 = 0x4E74 (check again at end for any missed)
    if opcode == 0x4E74:
        return FAMILY_RTS
    
    # ABCD: 1100...100000... and 1100...100001... = 0xC100-0xC107, 0xC108-0xC10F
    if (opcode & 0xF1F8) == 0xC100 or (opcode & 0xF1F8) == 0xC108:
        return FAMILY_ABCD
    
    # ADDX: 1101...100000... and 1101...100001... = 0xD100-0xD107, 0xD108-0xD10F
    if (opcode & 0xF1F8) == 0xD100 or (opcode & 0xF1F8) == 0xD108:
        return FAMILY_ADDX
    
    # SUBX: 1001...100000... and 1001...100001... = 0x9100-0x9107, 0x9108-0x910F
    if (opcode & 0xF1F8) == 0x9100 or (opcode & 0xF1F8) == 0x9108:
        return FAMILY_SUBX
    
    # EOR: 1011...1..000... = 0xB100-0xB107, 0xB300-0xB307, etc.
    if (opcode & 0xF138) == 0xB100:
        return FAMILY_EOR
    
    # MULU: 1100...011...... = 0xC000-0xC03F
    if (opcode & 0xF1C0) == 0xC000:
        return FAMILY_MULU
    
    # MULS: 1100...011...... = 0xC040-0xC07F
    if (opcode & 0xF1C0) == 0xC040:
        return FAMILY_MULS
    
    # DIVU: 1000...011...... = 0x8000-0x803F
    if (opcode & 0xF1C0) == 0x8000:
        return FAMILY_DIVU
    
    # DIVS: 1000...011...... = 0x8040-0x807F
    if (opcode & 0xF1C0) == 0x8040:
        return FAMILY_DIVS
    
    # SCC: 0101010011...... = 0x54C0-0x54FF
    if (opcode & 0xFFC0) == 0x54C0:
        return FAMILY_SCC
    
    # JSR: 0100111011...... = 0x4ED0-0x4EDF (check again)
    if (opcode & 0xFFC0) == 0x4ED0:
        return FAMILY_JSR
    
    # MOVEP: 00001101........ and 00001100........ (0x0D00-0x0DFF, 0x0C00-0x0CFF)
    if (opcode & 0xF100) == 0x0100 or (opcode & 0xF100) == 0x0180:
        return FAMILY_MOVEP
    
    # EXTB: 0100100011000... = 0x49C0-0x49C7 (68020+)
    if (opcode & 0xFFF8) == 0x49C0:
        return FAMILY_EXTB
    
    return FAMILY_ILLEGAL


def parse_instruction_desc(desc, opcode_val=None):
    """
    Parse an instruction description like "MOVE.b D0, D1" or "ADD.w #, Dn"
    Returns (family, size, src_ea, dst_ea, ext_words, flags)
    
    Uses opcode bit patterns for accurate family classification.
    """
    if desc == "None":
        return {
            'family': FAMILY_ILLEGAL,
            'size': SIZE_NONE,
            'src_ea': EA_NONE,
            'dst_ea': EA_NONE,
            'ext_words': 0,
            'reads_ccr': 0,
            'writes_ccr': 0,
            'privileged': 0,
            'may_trap': 0,
            'block_end': 0
        }

    family = FAMILY_ILLEGAL
    size = SIZE_NONE
    src_ea = EA_NONE
    dst_ea = EA_NONE
    ext_words = 0
    reads_ccr = 0
    writes_ccr = 0
    privileged = 0
    may_trap = 0
    block_end = 0

    # Remove extra spaces
    desc = ' '.join(desc.split())

    # Check for size suffix
    size_match = re.search(r'\.(B|W|L|32|16|8)\b', desc, re.IGNORECASE)
    if size_match:
        s = size_match.group(1).upper()
        if s in ('B', '8'):
            size = SIZE_BYTE
        elif s in ('W', '16'):
            size = SIZE_WORD
        elif s in ('L', '32'):
            size = SIZE_LONG

    # Use opcode bit patterns for accurate family classification
    # This is more reliable than parsing description strings
    if opcode_val is not None:
        family = get_family_from_opcode(opcode_val, desc)
        if family != FAMILY_ILLEGAL or desc == "None":
            # Got a match from opcode pattern, skip string parsing
            result = {
                'family': family,
                'size': size,
                'src_ea': src_ea,
                'dst_ea': dst_ea,
                'ext_words': ext_words,
                'reads_ccr': reads_ccr,
                'writes_ccr': writes_ccr,
                'privileged': privileged,
                'may_trap': may_trap,
                'block_end': block_end
            }
            # Still parse EA and CCR effects from description
            # (these are handled later in the function)
            return result

    # Determine family from instruction name (fallback)
    desc_upper = desc.upper()

    # Check for specific instruction patterns
    if desc_upper.startswith('ILLEGAL') or 'LINEA' in desc_upper or 'LINEF' in desc_upper:
        family = FAMILY_ILL
        if 'LINEA' in desc_upper:
            family = FAMILY_LINE_A
        elif 'LINEF' in desc_upper:
            family = FAMILY_LINE_F
    elif desc_upper.startswith('MOVE16'):
        family = FAMILY_MOVE16
    elif desc_upper.startswith('MOVEP'):
        family = FAMILY_MOVEP
        ext_words = 1  # displacement
    elif desc_upper.startswith('MOVEM'):
        family = FAMILY_MOVEM
        ext_words = 2  # register mask + displacement/absolute
    elif desc_upper.startswith('MOVEQ'):
        family = FAMILY_MOVEQ
        ext_words = 0  # immediate in opcode
    elif desc_upper.startswith('MOVE') and ' CCR' in desc_upper:
        family = FAMILY_MOVE_CCR
    elif desc_upper.startswith('MOVE') and ' SR' in desc_upper:
        family = FAMILY_MOVE_SR
        privileged = 1
    elif desc_upper.startswith('MOVE') and ' USP' in desc_upper:
        family = FAMILY_MOVE_USP
        privileged = 1
    elif desc_upper.startswith('MOVEC'):
        family = FAMILY_MOVEC
        privileged = 1
    elif desc_upper.startswith('MOVE'):
        family = FAMILY_MOVE
    elif desc_upper.startswith('ADDQ'):
        family = FAMILY_ADDQ
    elif desc_upper.startswith('ADDX'):
        family = FAMILY_ADDX
    elif desc_upper.startswith('ADD'):
        family = FAMILY_ADD
    elif desc_upper.startswith('SUBQ'):
        family = FAMILY_SUBQ
    elif desc_upper.startswith('SUBX'):
        family = FAMILY_SUBX
    elif desc_upper.startswith('SUB'):
        family = FAMILY_SUB
    elif desc_upper.startswith('CMPM'):
        family = FAMILY_CMPM
    elif desc_upper.startswith('CMP2'):
        family = FAMILY_CMP2
    elif desc_upper.startswith('CMP'):
        family = FAMILY_CMP
    elif desc_upper.startswith('ANDI') and ' CCR' in desc_upper:
        family = FAMILY_ANDI_CCR
        ext_words = 1
    elif desc_upper.startswith('ANDI') and ' SR' in desc_upper:
        family = FAMILY_ANDI_SR
        privileged = 1
        ext_words = 1
    elif desc_upper.startswith('ANDI'):
        family = FAMILY_ANDI
        ext_words = 1
    elif desc_upper.startswith('ORI') and ' CCR' in desc_upper:
        family = FAMILY_ORI_CCR
        ext_words = 1
    elif desc_upper.startswith('ORI') and ' SR' in desc_upper:
        family = FAMILY_ORI_SR
        privileged = 1
        ext_words = 1
    elif desc_upper.startswith('ORI'):
        family = FAMILY_ORI
        ext_words = 1
    elif desc_upper.startswith('EORI') and ' CCR' in desc_upper:
        family = FAMILY_EORI_CCR
        ext_words = 1
    elif desc_upper.startswith('EORI') and ' SR' in desc_upper:
        family = FAMILY_EORI_SR
        privileged = 1
        ext_words = 1
    elif desc_upper.startswith('EORI'):
        family = FAMILY_EORI
        ext_words = 1
    elif desc_upper.startswith('CMPI'):
        family = FAMILY_CMPI
        ext_words = 1
    elif desc_upper.startswith('SUBI'):
        family = FAMILY_SUBI
        ext_words = 1
    elif desc_upper.startswith('ADDI'):
        family = FAMILY_ADDI
        ext_words = 1
    elif desc_upper.startswith('MULS'):
        family = FAMILY_MULS
    elif desc_upper.startswith('MULU'):
        family = FAMILY_MULU
    elif desc_upper.startswith('DIVS'):
        family = FAMILY_DIVS
    elif desc_upper.startswith('DIVU'):
        family = FAMILY_DIVU
    elif desc_upper.startswith('DIVL'):
        family = FAMILY_DIVL
    elif desc_upper.startswith('MULL'):
        family = FAMILY_MULL
    elif desc_upper.startswith('ABC'):
        family = FAMILY_ABCD
    elif desc_upper.startswith('NEGX'):
        family = FAMILY_NEGX
    elif desc_upper.startswith('NEG'):
        family = FAMILY_NEG
    elif desc_upper.startswith('NOT'):
        family = FAMILY_NOT
    elif desc_upper.startswith('CLR'):
        family = FAMILY_CLR
    elif desc_upper.startswith('TST'):
        family = FAMILY_TST
    elif desc_upper.startswith('TAS'):
        family = FAMILY_TAS
        ext_words = 0
    elif desc_upper.startswith('BTST'):
        family = FAMILY_BTST
    elif desc_upper.startswith('BSET'):
        family = FAMILY_BSET
    elif desc_upper.startswith('BCLR'):
        family = FAMILY_BCLR
    elif desc_upper.startswith('BCHG'):
        family = FAMILY_BCHG
    elif desc_upper.startswith('SCC') or desc_upper.startswith('S'):
        family = FAMILY_SCC
    elif desc_upper.startswith('DBCC') or desc_upper.startswith('DB'):
        family = FAMILY_DBCC
        ext_words = 1
    elif desc_upper.startswith('BSR'):
        family = FAMILY_BSR
        if size == SIZE_LONG:
            ext_words = 2
        else:
            ext_words = 1
        block_end = 1
    elif desc_upper.startswith('BCC') or re.match(r'B(CC|CS|HI|LS|CC|EQ|NE|VC|VS|PL|MI|GE|LT|GT|LE|RA|GE|LT)\b', desc_upper):
        family = FAMILY_BCC
        if size == SIZE_LONG:
            ext_words = 2
        else:
            ext_words = 1
        block_end = 1
    elif desc_upper.startswith('BRA'):
        family = FAMILY_BRA
        if size == SIZE_LONG:
            ext_words = 2
        else:
            ext_words = 1
        block_end = 1
    elif desc_upper.startswith('JMP'):
        family = FAMILY_JMP
        block_end = 1
    elif desc_upper.startswith('JSR'):
        family = FAMILY_JSR
        block_end = 1
    elif desc_upper.startswith('RTS'):
        family = FAMILY_RTS
        block_end = 1
    elif desc_upper.startswith('RTR'):
        family = FAMILY_RTR
        block_end = 1
    elif desc_upper.startswith('RTE'):
        family = FAMILY_RTE
        privileged = 1
        block_end = 1
    elif desc_upper.startswith('RTD'):
        family = FAMILY_RTD
        ext_words = 1
        block_end = 1
    elif desc_upper.startswith('LINK'):
        family = FAMILY_LINK
        ext_words = 1
    elif desc_upper.startswith('UNLK'):
        family = FAMILY_UNLK
    elif desc_upper.startswith('RESET'):
        family = FAMILY_RESET
        privileged = 1
    elif desc_upper.startswith('NOP'):
        family = FAMILY_NOP
    elif desc_upper.startswith('STOP'):
        family = FAMILY_STOP
        privileged = 1
        ext_words = 1
        block_end = 1
    elif desc_upper.startswith('ASL'):
        family = FAMILY_ASL
    elif desc_upper.startswith('ASR'):
        family = FAMILY_ASR
    elif desc_upper.startswith('LSL'):
        family = FAMILY_LSL
    elif desc_upper.startswith('LSR'):
        family = FAMILY_LSR
    elif desc_upper.startswith('ROL'):
        family = FAMILY_ROL
    elif desc_upper.startswith('ROR'):
        family = FAMILY_ROR
    elif desc_upper.startswith('ROXL'):
        family = FAMILY_ROXL
    elif desc_upper.startswith('ROXR'):
        family = FAMILY_ROXR
    elif desc_upper.startswith('SWAP'):
        family = FAMILY_SWAP
    elif desc_upper.startswith('PEA'):
        family = FAMILY_PEA
        ext_words = 0
    elif desc_upper.startswith('EXT'):
        family = FAMILY_EXT
    elif desc_upper.startswith('EXTB'):
        family = FAMILY_EXTB
    elif desc_upper.startswith('LEA'):
        family = FAMILY_LEA
    elif desc_upper.startswith('CHK2'):
        family = FAMILY_CHK2
    elif desc_upper.startswith('CHK'):
        family = FAMILY_CHK
    elif desc_upper.startswith('TRAPV'):
        family = FAMILY_TRAPV
        may_trap = 1
    elif desc_upper.startswith('TRAP'):
        family = FAMILY_TRAP
        may_trap = 1
    elif desc_upper.startswith('BKPT'):
        family = FAMILY_BKPT
        may_trap = 1
    elif desc_upper.startswith('CALLM'):
        family = FAMILY_CALLM
        ext_words = 1
    elif desc_upper.startswith('RTM'):
        family = FAMILY_RTM
    elif desc_upper.startswith('TRAPCC') or desc_upper.startswith('TRAP'):
        family = FAMILY_TRAPCC
        ext_words = 1
        may_trap = 1
    elif desc_upper.startswith('CAS'):
        family = FAMILY_CAS
    elif desc_upper.startswith('CAS2'):
        family = FAMILY_CAS2
    elif desc_upper.startswith('BF'):
        family = FAMILY_BF
        ext_words = 1
    elif desc_upper.startswith('PACK'):
        family = FAMILY_PACK
        ext_words = 1
    elif desc_upper.startswith('UNPK'):
        family = FAMILY_UNPK
        ext_words = 1
    elif desc_upper.startswith('CP'):
        family = FAMILY_CP
    elif desc_upper.startswith('NBC'):
        family = FAMILY_NOP
    elif desc_upper.startswith('OR'):
        family = FAMILY_OR
    elif desc_upper.startswith('EOR'):
        family = FAMILY_EOR
    else:
        family = FAMILY_ILLEGAL

    # Parse EA modes from description
    # Format is typically "INSTR.size SRC, DST" or "INSTR.size SRC, EA"
    if desc != "None" and family != FAMILY_ILLEGAL:
        # Try to extract EA info
        parts = desc.split(',')
        if len(parts) >= 1:
            # First part is instruction and source
            first_part = parts[0].strip()
            # Remove instruction name and size
            instr_match = re.match(r'[A-Z0-9]+(\.[BWL])?\s*(.*)', first_part, re.IGNORECASE)
            if instr_match:
                src_str = instr_match.group(2).strip()
                src_ea = parse_ea_mode(src_str)

        if len(parts) >= 2:
            dst_str = parts[1].strip()
            dst_ea = parse_ea_mode(dst_str)

    # Determine CCR effects based on family
    reads_ccr, writes_ccr = get_ccr_effects(family, desc_upper)

    # Determine if instruction may trap
    if family in (FAMILY_TRAP, FAMILY_TRAPV, FAMILY_TRAPCC, FAMILY_BKPT,
                  FAMILY_JSR, FAMILY_JMP, FAMILY_RTE, FAMILY_RESET,
                  FAMILY_LINE_A, FAMILY_LINE_F, FAMILY_ILL, FAMILY_STOP,
                  FAMILY_DIVU, FAMILY_DIVS, FAMILY_CHK, FAMILY_CHK2):
        may_trap = 1

    # Determine if instruction ends a block
    if family in (FAMILY_BRA, FAMILY_BCC, FAMILY_BSR, FAMILY_JMP, FAMILY_JSR,
                  FAMILY_RTS, FAMILY_RTR, FAMILY_RTE, FAMILY_RTD, FAMILY_STOP,
                  FAMILY_RTM, FAMILY_TRAP, FAMILY_TRAPV, FAMILY_TRAPCC,
                  FAMILY_LINE_A, FAMILY_LINE_F, FAMILY_ILL):
        block_end = 1

    # Determine if privileged
    if family in (FAMILY_STOP, FAMILY_RTE, FAMILY_RESET, FAMILY_MOVEC,
                  FAMILY_MOVE_SR, FAMILY_MOVE_USP, FAMILY_ORI_SR,
                  FAMILY_ANDI_SR, FAMILY_EORI_SR):
        privileged = 1

    return {
        'family': family,
        'size': size,
        'src_ea': src_ea,
        'dst_ea': dst_ea,
        'ext_words': ext_words,
        'reads_ccr': reads_ccr,
        'writes_ccr': writes_ccr,
        'privileged': privileged,
        'may_trap': may_trap,
        'block_end': block_end
    }


def parse_ea_mode(ea_str):
    """Parse an EA mode string and return EA class."""
    ea_str = ea_str.strip().upper()

    if not ea_str or ea_str == '.':
        return EA_NONE

    # Data register
    if re.match(r'^D[0-7N]$', ea_str):
        return EA_DN

    # Address register
    if re.match(r'^A[0-7N]$', ea_str):
        return EA_AN

    # Address register indirect
    if re.match(r'^\(A[0-7]\)$', ea_str):
        return EA_AI

    # Postincrement
    if re.match(r'^\(A[0-7]\)\+$', ea_str):
        return EA_PI

    # Predecrement
    if re.match(r'^-\(A[0-7]\)$', ea_str):
        return EA_PD

    # Displacement
    if re.match(r'^\(D[0-9],A[0-7]\)$', ea_str):
        return EA_IX
    if re.match(r'^\(A[0-7],XN\)$', ea_str):
        return EA_IX
    if re.match(r'^\(D[0-9],A[0-7],XN\)$', ea_str):
        return EA_IX
    if re.match(r'^\(D8,A[0-7],XN\)$', ea_str):
        return EA_IX

    # Absolute word/long - (xxx).w or (xxx).l
    if re.match(r'^\(XXX\)\.W$', ea_str):
        return EA_AW
    if re.match(r'^\(XXX\)\.L$', ea_str):
        return EA_AL

    # PC relative
    if re.match(r'^\(PC,D[0-9]\)$', ea_str):
        return EA_PCDI
    if re.match(r'^\(PC,D[0-9],XN\)$', ea_str):
        return EA_PCIX
    if re.match(r'^\(PC,XN\)$', ea_str):
        return EA_PCIX

    # Immediate
    if ea_str == '#' or ea_str == '#,':
        return EA_IMM
    if ea_str.startswith('#'):
        return EA_IMM

    # Displacement on address register (d16, An)
    if re.match(r'^\(D16,A[0-7]\)$', ea_str):
        return EA_DI

    return EA_NONE


def get_ccr_effects(family, desc_upper):
    """Return (reads_ccr, writes_ccr) mask for the given family."""
    reads = 0
    writes = 0

    # Instructions that read CCR
    if family in (FAMILY_BCC, FAMILY_SCC, FAMILY_DBCC, FAMILY_TRAPCC,
                  FAMILY_ABCD, FAMILY_ADDX, FAMILY_SUBX, FAMILY_NEGX,
                  FAMILY_ROXL, FAMILY_ROXR):
        reads = CCR_X

    if family in (FAMILY_BCC, FAMILY_SCC, FAMILY_DBCC, FAMILY_TRAPCC):
        reads |= CCR_ALL & ~CCR_X  # Read N Z V C

    # Instructions that write CCR
    if family in (FAMILY_ADD, FAMILY_ADDQ, FAMILY_ADDX, FAMILY_SUB,
                  FAMILY_SUBQ, FAMILY_SUBX, FAMILY_CMP, FAMILY_CMPM,
                  FAMILY_AND, FAMILY_ANDI, FAMILY_OR, FAMILY_ORI,
                  FAMILY_EOR, FAMILY_EORI, FAMILY_CMPI, FAMILY_SUBI,
                  FAMILY_ADDI, FAMILY_NEG, FAMILY_NEGX, FAMILY_NOT,
                  FAMILY_CLR, FAMILY_TST, FAMILY_ABCD,
                  FAMILY_ASL, FAMILY_ASR, FAMILY_LSL, FAMILY_LSR,
                  FAMILY_ROL, FAMILY_ROR, FAMILY_ROXL, FAMILY_ROXR,
                  FAMILY_ORI_CCR, FAMILY_ANDI_CCR, FAMILY_EORI_CCR,
                  FAMILY_MOVE_CCR):
        writes = CCR_ALL

    # Some instructions only write certain flags
    if family in (FAMILY_MOVE, FAMILY_MOVEQ, FAMILY_MOVEP, FAMILY_MOVEM,
                  FAMILY_LEA, FAMILY_PEA):
        writes = CCR_N | CCR_Z | CCR_V  # N Z V, clears X and C

    if family in (FAMILY_EXT,):
        writes = CCR_N | CCR_Z | CCR_V  # N Z V

    if family in (FAMILY_SWAP,):
        writes = CCR_N | CCR_Z  # N Z only

    if family in (FAMILY_CLR,):
        writes = CCR_ALL
        reads = 0

    if family in (FAMILY_TST,):
        writes = CCR_N | CCR_Z | CCR_V  # N Z V, clears X and C

    # ORI/ANDI/EORI to CCR only write affected bits
    if family in (FAMILY_ORI_CCR, FAMILY_ANDI_CCR, FAMILY_EORI_CCR):
        writes = CCR_ALL
        reads = CCR_ALL

    # MOVE from CCR reads all
    if family in (FAMILY_MOVE_CCR,):
        reads = CCR_ALL

    # MOVE to SR reads CCR and writes all
    if family in (FAMILY_MOVE_SR,):
        reads = CCR_ALL
        writes = CCR_ALL

    return reads, writes


def generate_header_file(output_path):
    """Generate the header file with opcode info structures."""
    content = """/*
 * JIT Opcode Information Header
 * Generated by generate_opinfo.py
 *
 * This file contains opcode metadata for the 68000 JIT.
 */

#ifndef JIT_68000_OPINFO_H
#define JIT_68000_OPINFO_H

#include <stdint.h>

/* EA (Effective Address) classes */
#define JIT_EA_NONE     0    /* No EA */
#define JIT_EA_DN       1    /* Data register direct */
#define JIT_EA_AN       2    /* Address register direct */
#define JIT_EA_AI       3    /* Address register indirect */
#define JIT_EA_PI       4    /* Address register indirect with postincrement */
#define JIT_EA_PD       5    /* Address register indirect with predecrement */
#define JIT_EA_DI       6    /* Address register indirect with displacement */
#define JIT_EA_IX       7    /* Address register indirect with index */
#define JIT_EA_AW       8    /* Absolute word */
#define JIT_EA_AL       9    /* Absolute long */
#define JIT_EA_PCDI     10   /* PC relative with displacement */
#define JIT_EA_PCIX     11   /* PC relative with index */
#define JIT_EA_IMM      12   /* Immediate */

/* Size kinds */
#define JIT_SIZE_NONE   0
#define JIT_SIZE_BYTE   1
#define JIT_SIZE_WORD   2
#define JIT_SIZE_LONG   3

/* Handler families */
#define JIT_FAMILY_ILLEGAL      0
#define JIT_FAMILY_MOVE         1
#define JIT_FAMILY_MOVEQ        2
#define JIT_FAMILY_MOVEP        3
#define JIT_FAMILY_MOVEM        4
#define JIT_FAMILY_MOVE16       5
#define JIT_FAMILY_ADD          6
#define JIT_FAMILY_ADDQ         7
#define JIT_FAMILY_ADDX         8
#define JIT_FAMILY_SUB          9
#define JIT_FAMILY_SUBQ         10
#define JIT_FAMILY_SUBX         11
#define JIT_FAMILY_CMP          12
#define JIT_FAMILY_CMPM         13
#define JIT_FAMILY_AND          14
#define JIT_FAMILY_OR           15
#define JIT_FAMILY_EOR          16
#define JIT_FAMILY_ABCD         17
#define JIT_FAMILY_NEG          18
#define JIT_FAMILY_NEGX         19
#define JIT_FAMILY_NOT          20
#define JIT_FAMILY_CLR          21
#define JIT_FAMILY_TST          22
#define JIT_FAMILY_TAS          23
#define JIT_FAMILY_BTST         24
#define JIT_FAMILY_BSET         25
#define JIT_FAMILY_BCLR         26
#define JIT_FAMILY_BCHG         27
#define JIT_FAMILY_SCC          28
#define JIT_FAMILY_DBCC         29
#define JIT_FAMILY_BCC          30
#define JIT_FAMILY_BSR          31
#define JIT_FAMILY_BRA          32
#define JIT_FAMILY_JMP          33
#define JIT_FAMILY_JSR          34
#define JIT_FAMILY_RTS          35
#define JIT_FAMILY_RTR          36
#define JIT_FAMILY_RTE          37
#define JIT_FAMILY_RTD          38
#define JIT_FAMILY_LINK         39
#define JIT_FAMILY_UNLK         40
#define JIT_FAMILY_RESET        41
#define JIT_FAMILY_NOP          42
#define JIT_FAMILY_STOP         43
#define JIT_FAMILY_ANDI         44
#define JIT_FAMILY_ORI          45
#define JIT_FAMILY_EORI         46
#define JIT_FAMILY_CMPI         47
#define JIT_FAMILY_SUBI         48
#define JIT_FAMILY_ADDI         49
#define JIT_FAMILY_MULU         50
#define JIT_FAMILY_MULS         51
#define JIT_FAMILY_DIVU         52
#define JIT_FAMILY_DIVS         53
#define JIT_FAMILY_ASL          54
#define JIT_FAMILY_ASR          55
#define JIT_FAMILY_LSL          56
#define JIT_FAMILY_LSR          57
#define JIT_FAMILY_ROL          58
#define JIT_FAMILY_ROR          59
#define JIT_FAMILY_ROXL         60
#define JIT_FAMILY_ROXR         61
#define JIT_FAMILY_SWAP         62
#define JIT_FAMILY_PEA          63
#define JIT_FAMILY_EXT          64
#define JIT_FAMILY_EXTB         65
#define JIT_FAMILY_LEA          66
#define JIT_FAMILY_CHK          67
#define JIT_FAMILY_LINE_A       68
#define JIT_FAMILY_LINE_F       69
#define JIT_FAMILY_TRAP         70
#define JIT_FAMILY_TRAPV        71
#define JIT_FAMILY_ILL          72
#define JIT_FAMILY_ORI_CCR      73
#define JIT_FAMILY_ANDI_CCR     74
#define JIT_FAMILY_EORI_CCR     75
#define JIT_FAMILY_ORI_SR       76
#define JIT_FAMILY_ANDI_SR      77
#define JIT_FAMILY_EORI_SR      78
#define JIT_FAMILY_MOVE_CCR     79
#define JIT_FAMILY_MOVE_SR      80
#define JIT_FAMILY_MOVE_USP     81
#define JIT_FAMILY_MOVEC        82
#define JIT_FAMILY_BKPT         83
#define JIT_FAMILY_CALLM        84
#define JIT_FAMILY_RTM          85
#define JIT_FAMILY_TRAPCC       86
#define JIT_FAMILY_CP           87
#define JIT_FAMILY_CAS          88
#define JIT_FAMILY_CAS2         89
#define JIT_FAMILY_CHK2         90
#define JIT_FAMILY_CMP2         91
#define JIT_FAMILY_DIVL         92
#define JIT_FAMILY_MULL         93
#define JIT_FAMILY_PACK         94
#define JIT_FAMILY_UNPK         95
#define JIT_FAMILY_BF           96

/* CCR flags */
#define JIT_CCR_X  0x10
#define JIT_CCR_N  0x08
#define JIT_CCR_Z  0x04
#define JIT_CCR_V  0x02
#define JIT_CCR_C  0x01
#define JIT_CCR_ALL  0x1F

/* Opcode info structure - packed for cache efficiency */
typedef struct {
    uint8_t  family;       /* Handler family */
    uint8_t  size;         /* Size kind */
    uint8_t  src_ea;       /* Source EA class */
    uint8_t  dst_ea;       /* Destination EA class */
    uint8_t  ext_words;    /* Extension word count */
    uint8_t  reads_ccr;    /* CCR bits read */
    uint8_t  writes_ccr;   /* CCR bits written */
    uint8_t  flags;        /* Privileged, may_trap, block_end */
} jit_opinfo_t;

/* Flag bits */
#define JIT_OPF_PRIVILEGED  0x01
#define JIT_OPF_MAY_TRAP    0x02
#define JIT_OPF_BLOCK_END   0x04

/* Accessor macros */
#define JIT_OPINFO_PRIVILEGED(op)  ((op)->flags & JIT_OPF_PRIVILEGED)
#define JIT_OPINFO_MAY_TRAP(op)    ((op)->flags & JIT_OPF_MAY_TRAP)
#define JIT_OPINFO_BLOCK_END(op)   ((op)->flags & JIT_OPF_BLOCK_END)

/* External declaration of the opcode info table */
extern const jit_opinfo_t g_jit_opinfo_table[0x10000];

/* Get opcode info for a given opcode */
static inline const jit_opinfo_t* jit_get_opinfo(uint16_t opcode) {
    return &g_jit_opinfo_table[opcode];
}

#endif /* JIT_68000_OPINFO_H */
"""
    with open(output_path, 'w') as f:
        f.write(content)


def generate_source_file(output_path, opinfo_data):
    """Generate the source file with the opcode info table."""
    lines = []
    lines.append("/*")
    lines.append(" * JIT Opcode Information Table")
    lines.append(" * Generated by generate_opinfo.py")
    lines.append(" *")
    lines.append(" * This file contains opcode metadata for all 65536 opcodes.")
    lines.append(" */")
    lines.append("")
    lines.append("#include \"jit_68000_opinfo.h\"")
    lines.append("")
    lines.append("/* Opcode info table - 65536 entries */")
    lines.append("const jit_opinfo_t g_jit_opinfo_table[0x10000] = {")

    for opcode in range(0x10000):
        info = opinfo_data.get(opcode, {
            'family': 0, 'size': 0, 'src_ea': 0, 'dst_ea': 0,
            'ext_words': 0, 'reads_ccr': 0, 'writes_ccr': 0, 'flags': 0
        })

        flags = 0
        if info.get('privileged', 0):
            flags |= 0x01
        if info.get('may_trap', 0):
            flags |= 0x02
        if info.get('block_end', 0):
            flags |= 0x04

        line = "    {{JIT_FAMILY_{:<12}, JIT_SIZE_{:<5}, JIT_EA_{:<6}, JIT_EA_{:<6}, {:2}, 0x{:02X}, 0x{:02X}, 0x{:02X}}},".format(
            get_family_name(info['family']),
            get_size_name(info['size']),
            get_ea_name(info['src_ea']),
            get_ea_name(info['dst_ea']),
            info['ext_words'],
            info['reads_ccr'],
            info['writes_ccr'],
            flags
        )
        lines.append(line)

    lines.append("};")
    lines.append("")

    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))


def get_family_name(family_id):
    """Get the family name string for a family ID."""
    names = {
        0: 'ILLEGAL', 1: 'MOVE', 2: 'MOVEQ', 3: 'MOVEP', 4: 'MOVEM',
        5: 'MOVE16', 6: 'ADD', 7: 'ADDQ', 8: 'ADDX', 9: 'SUB',
        10: 'SUBQ', 11: 'SUBX', 12: 'CMP', 13: 'CMPM', 14: 'AND',
        15: 'OR', 16: 'EOR', 17: 'ABCD', 18: 'NEG', 19: 'NEGX',
        20: 'NOT', 21: 'CLR', 22: 'TST', 23: 'TAS', 24: 'BTST',
        25: 'BSET', 26: 'BCLR', 27: 'BCHG', 28: 'SCC', 29: 'DBCC',
        30: 'BCC', 31: 'BSR', 32: 'BRA', 33: 'JMP', 34: 'JSR',
        35: 'RTS', 36: 'RTR', 37: 'RTE', 38: 'RTD', 39: 'LINK',
        40: 'UNLK', 41: 'RESET', 42: 'NOP', 43: 'STOP', 44: 'ANDI',
        45: 'ORI', 46: 'EORI', 47: 'CMPI', 48: 'SUBI', 49: 'ADDI',
        50: 'MULU', 51: 'MULS', 52: 'DIVU', 53: 'DIVS', 54: 'ASL',
        55: 'ASR', 56: 'LSL', 57: 'LSR', 58: 'ROL', 59: 'ROR',
        60: 'ROXL', 61: 'ROXR', 62: 'SWAP', 63: 'PEA', 64: 'EXT',
        65: 'EXTB', 66: 'LEA', 67: 'CHK', 68: 'LINE_A', 69: 'LINE_F',
        70: 'TRAP', 71: 'TRAPV', 72: 'ILL', 73: 'ORI_CCR', 74: 'ANDI_CCR',
        75: 'EORI_CCR', 76: 'ORI_SR', 77: 'ANDI_SR', 78: 'EORI_SR',
        79: 'MOVE_CCR', 80: 'MOVE_SR', 81: 'MOVE_USP', 82: 'MOVEC',
        83: 'BKPT', 84: 'CALLM', 85: 'RTM', 86: 'TRAPCC', 87: 'CP',
        88: 'CAS', 89: 'CAS2', 90: 'CHK2', 91: 'CMP2', 92: 'DIVL',
        93: 'MULL', 94: 'PACK', 95: 'UNPK', 96: 'BF'
    }
    return names.get(family_id, 'ILLEGAL')


def get_size_name(size_id):
    """Get the size name string for a size ID."""
    names = {0: 'NONE', 1: 'BYTE', 2: 'WORD', 3: 'LONG'}
    return names.get(size_id, 'NONE')


def get_ea_name(ea_id):
    """Get the EA name string for an EA ID."""
    names = {
        0: 'NONE', 1: 'DN', 2: 'AN', 3: 'AI', 4: 'PI',
        5: 'PD', 6: 'DI', 7: 'IX', 8: 'AW', 9: 'AL',
        10: 'PCDI', 11: 'PCIX', 12: 'IMM'
    }
    return names.get(ea_id, 'NONE')


def main():
    if len(sys.argv) < 2:
        print("Usage: generate_opinfo.py <path_to_68000.official.json> [output_dir]")
        sys.exit(1)

    json_path = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(json_path)

    # Load opcode JSON
    print(f"Loading opcode data from {json_path}...")
    with open(json_path, 'r') as f:
        opcode_map = json.load(f)

    print(f"Processing {len(opcode_map)} opcodes...")

    # Parse all opcodes
    opinfo_data = {}
    for opcode_str, desc in opcode_map.items():
        opcode = int(opcode_str, 16)
        info = parse_instruction_desc(desc, opcode)
        opinfo_data[opcode] = info

    # Generate header file
    header_path = os.path.join(output_dir, 'jit_68000_opinfo.h')
    print(f"Generating {header_path}...")
    generate_header_file(header_path)

    # Generate source file
    source_path = os.path.join(output_dir, 'jit_68000_opinfo.c')
    print(f"Generating {source_path}...")
    generate_source_file(source_path, opinfo_data)

    print("Done!")

    # Print summary
    family_counts = {}
    for info in opinfo_data.values():
        fam = info['family']
        family_counts[fam] = family_counts.get(fam, 0) + 1

    print("\\nOpcode family distribution:")
    for fam, count in sorted(family_counts.items(), key=lambda x: -x[1])[:20]:
        print(f"  {get_family_name(fam)}: {count}")


if __name__ == '__main__':
    main()
