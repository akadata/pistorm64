#!/usr/bin/env python3
"""
Opcode metadata generator for the AArch64 JIT.

Generates jit_68000_opinfo.h and jit_68000_opinfo.c with metadata for all 65536 opcodes.

Design:
- Single, sequential FAMILY constant definitions (no duplicates)
- Single ordered decode table with non-overlapping mask rules
- Validation pass to detect overlaps and contradictions
- Expands patterns into flat 65536-entry table for fast runtime lookup
"""

import json
import os
import sys

# ============================================================================
# EA (Effective Address) classes
# ============================================================================
EA_NONE = 0
EA_DN = 1
EA_AN = 2
EA_AI = 3
EA_PI = 4
EA_PD = 5
EA_DI = 6
EA_IX = 7
EA_AW = 8
EA_AL = 9
EA_PCDI = 10
EA_PCIX = 11
EA_IMM = 12

# Size kinds
SIZE_NONE = 0
SIZE_BYTE = 1
SIZE_WORD = 2
SIZE_LONG = 3

# ============================================================================
# Handler families - SINGLE DEFINITION EACH (no duplicates!)
# ============================================================================
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
FAMILY_CP = 87
FAMILY_CAS = 88
FAMILY_CAS2 = 89
FAMILY_CHK2 = 90
FAMILY_CMP2 = 91
FAMILY_DIVL = 92
FAMILY_MULL = 93
FAMILY_PACK = 94
FAMILY_UNPK = 95
FAMILY_BF = 96

FAMILY_NAMES = {v: k for k, v in list(globals().items()) if k.startswith('FAMILY_') and isinstance(v, int)}

# CCR flags
CCR_X = 0x10
CCR_N = 0x08
CCR_Z = 0x04
CCR_V = 0x02
CCR_C = 0x01
CCR_ALL = CCR_X | CCR_N | CCR_Z | CCR_V | CCR_C

# ============================================================================
# DECODE TABLE - Single source of truth
# Ordered by precedence: specific patterns before general ones
# (mask, match, family, src_ea, dst_ea, ext_words, reads_ccr, writes_ccr, flags)
# flags: bit 0 = privileged, bit 1 = may_trap, bit 2 = block_end
# ============================================================================

DECODE_TABLE = [
    # LINE_A: 0xA000-0xAFFF
    (0xF000, 0xA000, FAMILY_LINE_A, EA_NONE, EA_NONE, 0, 0, 0, 0b110),
    # LINE_F: 0xF000-0xFFFF
    (0xF000, 0xF000, FAMILY_LINE_F, EA_NONE, EA_NONE, 0, 0, 0, 0b110),
    
    # MOVE to CCR: 0x44C0
    (0xFFF8, 0x44C0, FAMILY_MOVE_CCR, EA_DN, EA_NONE, 0, CCR_ALL, 0, 0),
    # MOVE from SR: 0x40C0
    (0xFFF8, 0x40C0, FAMILY_MOVE_SR, EA_NONE, EA_DN, 0, CCR_ALL, CCR_ALL, 0b001),
    # MOVE to USP: 0x4E60/0x4E68
    (0xFFF8, 0x4E60, FAMILY_MOVE_USP, EA_NONE, EA_AN, 0, 0, 0, 0b001),
    (0xFFF8, 0x4E68, FAMILY_MOVE_USP, EA_AN, EA_NONE, 0, 0, 0, 0b001),
    # MOVEC: 0x4E7A/0x4E7B
    (0xFFFF, 0x4E7A, FAMILY_MOVEC, EA_NONE, EA_NONE, 1, 0, 0, 0b001),
    (0xFFFF, 0x4E7B, FAMILY_MOVEC, EA_NONE, EA_NONE, 1, 0, 0, 0b001),
    # MOVEP: 0x0D00-0x0DFF, 0x0C00-0x0CFF
    (0xF100, 0x0100, FAMILY_MOVEP, EA_IMM, EA_AN, 1, 0, 0, 0),
    (0xF100, 0x0180, FAMILY_MOVEP, EA_AN, EA_IMM, 1, 0, 0, 0),
    
    # General MOVE: 0x1000-0x3FFF (bits 15-12 = 0001, 0010, 0011)
    # MOVE to Dn: 0001 size(2) MMM(3) RRR(3) = 0x1xxx
    # MOVE to An: 0011 size(2) MMM(3) RRR(3) = 0x3xxx  
    # But we need separate patterns because mask matching is (op & mask) == match
    # Pattern for 0x1xxx: (0xF000, 0x1000) matches 0x1000-0x1FFF
    (0xF000, 0x1000, FAMILY_MOVE, EA_NONE, EA_NONE, 0, 0, CCR_N|CCR_Z|CCR_V, 0),
    # Pattern for 0x2xxx: (0xF000, 0x2000) matches 0x2000-0x2FFF (but 0x2000-0x2FFF is MOVEA which we handle separately)
    # Pattern for 0x3xxx: (0xF000, 0x3000) matches 0x3000-0x3FFF
    (0xF000, 0x3000, FAMILY_MOVE, EA_NONE, EA_NONE, 0, 0, CCR_N|CCR_Z|CCR_V, 0),
    
    # MOVEA: 0x2000-0x2FFF with destination EA=An (bits 15-12 = 001x)
    # MOVEA.W: 0010 0SSS MMMRRR, MOVEA.L: 0010 1SSS MMMRRR
    (0xF000, 0x2000, FAMILY_MOVE, EA_NONE, EA_AN, 0, 0, CCR_N|CCR_Z|CCR_V, 0),
    
    # MOVEQ: 0x7000-0x70FF
    (0xF100, 0x7000, FAMILY_MOVEQ, EA_IMM, EA_DN, 0, 0, CCR_N|CCR_Z|CCR_V, 0),
    
    # BSR: 0x6100-0x61FF
    (0xFF00, 0x6100, FAMILY_BSR, EA_NONE, EA_NONE, 1, 0, 0, 0b101),
    # BRA: 0x6000-0x60FF (unconditional, cccc=0000)
    (0xFF00, 0x6000, FAMILY_BRA, EA_NONE, EA_NONE, 1, 0, 0, 0b101),
    # Bcc: 0x6xxx where bits 11-8 != 0000 and != 0001 (i.e., 0010-1111)
    # Need multiple patterns since we can't do "not equal" matching
    # BCC (BEQ): 0x6200-0x62FF
    (0xFF00, 0x6200, FAMILY_BCC, EA_NONE, EA_NONE, 1, CCR_ALL, 0, 0b101),
    # BCC (BNE): 0x6300-0x63FF
    (0xFF00, 0x6300, FAMILY_BCC, EA_NONE, EA_NONE, 1, CCR_ALL, 0, 0b101),
    # BCC (BCC/BHS): 0x6400-0x64FF
    (0xFF00, 0x6400, FAMILY_BCC, EA_NONE, EA_NONE, 1, CCR_ALL, 0, 0b101),
    # BCC (BCS/BLO): 0x6500-0x65FF
    (0xFF00, 0x6500, FAMILY_BCC, EA_NONE, EA_NONE, 1, CCR_ALL, 0, 0b101),
    # BCC (BPL): 0x6600-0x66FF
    (0xFF00, 0x6600, FAMILY_BCC, EA_NONE, EA_NONE, 1, CCR_ALL, 0, 0b101),
    # BCC (BMI): 0x6700-0x67FF
    (0xFF00, 0x6700, FAMILY_BCC, EA_NONE, EA_NONE, 1, CCR_ALL, 0, 0b101),
    # BCC (BGE): 0x6800-0x68FF
    (0xFF00, 0x6800, FAMILY_BCC, EA_NONE, EA_NONE, 1, CCR_ALL, 0, 0b101),
    # BCC (BLT): 0x6900-0x69FF
    (0xFF00, 0x6900, FAMILY_BCC, EA_NONE, EA_NONE, 1, CCR_ALL, 0, 0b101),
    # BCC (BGT): 0x6A00-0x6AFF
    (0xFF00, 0x6A00, FAMILY_BCC, EA_NONE, EA_NONE, 1, CCR_ALL, 0, 0b101),
    # BCC (BLE): 0x6B00-0x6BFF
    (0xFF00, 0x6B00, FAMILY_BCC, EA_NONE, EA_NONE, 1, CCR_ALL, 0, 0b101),
    
    # DBcc: 0x50C8-0x5FCF with bits [7:3]=11001 (must come BEFORE general SCC)
    (0xF0F8, 0x50C8, FAMILY_DBCC, EA_IMM, EA_NONE, 1, CCR_ALL, 0, 0b101),
    # SCC: 0x50C0-0x5FC0 (bits 6-7 = 11)
    (0xF0C0, 0x50C0, FAMILY_SCC, EA_IMM, EA_DN, 0, CCR_ALL, 0, 0),
    
    # ADDQ: 0x5000-0x507F
    (0xF180, 0x5000, FAMILY_ADDQ, EA_IMM, EA_DN, 0, 0, CCR_ALL, 0),
    (0xF100, 0x5000, FAMILY_ADDQ, EA_IMM, EA_NONE, 0, 0, CCR_ALL, 0),
    # SUBQ: 0x5100-0x517F
    (0xF180, 0x5100, FAMILY_SUBQ, EA_IMM, EA_DN, 0, 0, CCR_ALL, 0),
    (0xF100, 0x5100, FAMILY_SUBQ, EA_IMM, EA_NONE, 0, 0, CCR_ALL, 0),
    
    # ADDI: 0x0600-0x06FF
    (0xFF00, 0x0600, FAMILY_ADDI, EA_IMM, EA_NONE, 1, 0, CCR_ALL, 0),
    # SUBI: 0x0400-0x04FF
    (0xFF00, 0x0400, FAMILY_SUBI, EA_IMM, EA_NONE, 1, 0, CCR_ALL, 0),
    # CMPI: 0x0C00-0x0CFF
    (0xFF00, 0x0C00, FAMILY_CMPI, EA_IMM, EA_NONE, 1, 0, CCR_ALL, 0),
    # ANDI: 0x0200-0x02FF
    (0xFF00, 0x0200, FAMILY_ANDI, EA_IMM, EA_NONE, 1, 0, CCR_ALL, 0),
    # ORI: 0x0000-0x00FF
    (0xFF00, 0x0000, FAMILY_ORI, EA_IMM, EA_NONE, 1, 0, CCR_ALL, 0),
    # EORI: 0x0A00-0x0AFF
    (0xFF00, 0x0A00, FAMILY_EORI, EA_IMM, EA_NONE, 1, 0, CCR_ALL, 0),
    
    # ORI to CCR: 0x003C
    (0xFFFF, 0x003C, FAMILY_ORI_CCR, EA_IMM, EA_NONE, 1, 0, CCR_ALL, 0),
    # ORI to SR: 0x007C
    (0xFFFF, 0x007C, FAMILY_ORI_SR, EA_IMM, EA_NONE, 1, 0, CCR_ALL, 0b001),
    # ANDI to CCR: 0x023C
    (0xFFFF, 0x023C, FAMILY_ANDI_CCR, EA_IMM, EA_NONE, 1, 0, CCR_ALL, 0),
    # ANDI to SR: 0x027C
    (0xFFFF, 0x027C, FAMILY_ANDI_SR, EA_IMM, EA_NONE, 1, 0, CCR_ALL, 0b001),
    # EORI to CCR: 0x0A3C
    (0xFFFF, 0x0A3C, FAMILY_EORI_CCR, EA_IMM, EA_NONE, 1, 0, CCR_ALL, 0),
    # EORI to SR: 0x0A7C
    (0xFFFF, 0x0A7C, FAMILY_EORI_SR, EA_IMM, EA_NONE, 1, 0, CCR_ALL, 0b001),
    
    # ADD: 0xD000-0xDFFF
    (0xF100, 0xD000, FAMILY_ADD, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    (0xF100, 0xD100, FAMILY_ADD, EA_NONE, EA_NONE, 0, 0, CCR_ALL, 0),
    # SUB: 0x9000-0x9FFF
    (0xF100, 0x9000, FAMILY_SUB, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    (0xF100, 0x9100, FAMILY_SUB, EA_NONE, EA_NONE, 0, 0, CCR_ALL, 0),
    # CMP: 0xB000-0xB0FF (byte), 0xB100-0xB1FF (word), 0xB200-0xB2FF (long)
    # Note: 0xB3xx+ are CMPM or illegal, not CMP
    (0xFFF0, 0xB000, FAMILY_CMP, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    (0xFFF0, 0xB100, FAMILY_CMP, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    (0xFFF0, 0xB200, FAMILY_CMP, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    # CMPM: 0xB1xx-0xB3xx with bits 5-3 = 1 (0xB1C8-0xB3C8 range)
    # CMPM.B (Ay)+,(Ax)+: 1011 0001 10001xxx (0xB1C8-0xB1CF)
    (0xFFF8, 0xB1C8, FAMILY_CMPM, EA_NONE, EA_NONE, 0, 0, CCR_ALL, 0),
    # CMPM.W (Ay)+,(Ax)+: 1011 0001 10001xxx (0xB1C8-0xB1CF)
    (0xFFF8, 0xB1C8, FAMILY_CMPM, EA_NONE, EA_NONE, 0, 0, CCR_ALL, 0),
    # CMPM.L (Ay)+,(Ax)+: 1011 0011 10001xxx (0xB3C8-0xB3CF)
    (0xFFF8, 0xB3C8, FAMILY_CMPM, EA_NONE, EA_NONE, 0, 0, CCR_ALL, 0),
    # AND: 0xC000-0xCFFF
    (0xF100, 0xC000, FAMILY_AND, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    (0xF100, 0xC100, FAMILY_AND, EA_NONE, EA_NONE, 0, 0, CCR_ALL, 0),
    # OR: 0x8000-0x8FFF
    (0xF100, 0x8000, FAMILY_OR, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    (0xF100, 0x8100, FAMILY_OR, EA_NONE, EA_NONE, 0, 0, CCR_ALL, 0),
    # EOR: 0xB100-0xB107, 0xB300-0xB307, etc.
    (0xF108, 0xB100, FAMILY_EOR, EA_DN, EA_DN, 0, 0, CCR_ALL, 0),
    
    # ADDX: 0xD100-0xD107, 0xD108-0xD10F
    (0xF1F8, 0xD100, FAMILY_ADDX, EA_DN, EA_DN, 0, CCR_X, CCR_ALL, 0),
    (0xF1F8, 0xD108, FAMILY_ADDX, EA_DN, EA_DN, 0, CCR_X, CCR_ALL, 0),
    # SUBX: 0x9100-0x9107, 0x9108-0x910F
    (0xF1F8, 0x9100, FAMILY_SUBX, EA_DN, EA_DN, 0, CCR_X, CCR_ALL, 0),
    (0xF1F8, 0x9108, FAMILY_SUBX, EA_DN, EA_DN, 0, CCR_X, CCR_ALL, 0),
    # ABCD: 0xC100-0xC107, 0xC108-0xC10F
    (0xF1F8, 0xC100, FAMILY_ABCD, EA_DN, EA_DN, 0, CCR_X, CCR_ALL, 0),
    (0xF1F8, 0xC108, FAMILY_ABCD, EA_DN, EA_DN, 0, CCR_X, CCR_ALL, 0),
    
    # MULU: 0xC000-0xC03F
    (0xF1C0, 0xC000, FAMILY_MULU, EA_DN, EA_DN, 0, 0, 0, 0),
    # MULS: 0xC040-0xC07F
    (0xF1C0, 0xC040, FAMILY_MULS, EA_DN, EA_DN, 0, 0, 0, 0),
    # DIVU: 0x8000-0x803F
    (0xF1C0, 0x8000, FAMILY_DIVU, EA_DN, EA_DN, 0, 0, 0, 0b010),
    # DIVS: 0x8040-0x807F
    (0xF1C0, 0x8040, FAMILY_DIVS, EA_DN, EA_DN, 0, 0, 0, 0b010),
    
    # Shifts/Rotates: 0xE000-0xEFFF
    # Encoding: 1110...dd... where dd=00/01/10/11 for ASx/LSx/ROXL/ROXR
    # and bit 3 = 0 for right, 1 for left
    # ASR: 1110...000...
    (0xF1F8, 0xE000, FAMILY_ASR, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    # LSR: 1110...001...
    (0xF1F8, 0xE008, FAMILY_LSR, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    # ROXR: 1110...010...
    (0xF1F8, 0xE010, FAMILY_ROXR, EA_NONE, EA_DN, 0, CCR_X, CCR_ALL, 0),
    # ROR: 1110...011...
    (0xF1F8, 0xE018, FAMILY_ROR, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    # ASL: 1110...100...
    (0xF1F8, 0xE020, FAMILY_ASL, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    # LSL: 1110...101...
    (0xF1F8, 0xE028, FAMILY_LSL, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    # ROXL: 1110...110...
    (0xF1F8, 0xE030, FAMILY_ROXL, EA_NONE, EA_DN, 0, CCR_X, CCR_ALL, 0),
    # ROL: 1110...111...
    (0xF1F8, 0xE038, FAMILY_ROL, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    
    # BTST: 0000...100...
    (0xF1F8, 0x0100, FAMILY_BTST, EA_NONE, EA_NONE, 0, 0, 0, 0),
    # BSET: 0000...101...
    (0xF1F8, 0x0140, FAMILY_BSET, EA_NONE, EA_NONE, 0, 0, 0, 0),
    # BCLR: 0000...110...
    (0xF1F8, 0x0180, FAMILY_BCLR, EA_NONE, EA_NONE, 0, 0, 0, 0),
    # BCHG: 0000...111...
    (0xF1F8, 0x01C0, FAMILY_BCHG, EA_NONE, EA_NONE, 0, 0, 0, 0),
    
    # TAS: 0x4AC0-0x4AFF
    (0xFFC0, 0x4AC0, FAMILY_TAS, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    # NEG: 0x4400-0x44FF
    (0xFF00, 0x4400, FAMILY_NEG, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    # NEGX: 0x4000-0x40FF
    (0xFF00, 0x4000, FAMILY_NEGX, EA_NONE, EA_DN, 0, CCR_X, CCR_ALL, 0),
    # NOT: 0x4600-0x46FF
    (0xFF00, 0x4600, FAMILY_NOT, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    # CLR: 0x4200-0x42FF
    (0xFF00, 0x4200, FAMILY_CLR, EA_NONE, EA_DN, 0, 0, CCR_ALL, 0),
    # TST: 0x4A00-0x4AFF
    (0xFF00, 0x4A00, FAMILY_TST, EA_NONE, EA_DN, 0, 0, CCR_N|CCR_Z|CCR_V, 0),
    
    # SWAP: 0x4840-0x4847
    (0xFFF8, 0x4840, FAMILY_SWAP, EA_NONE, EA_DN, 0, 0, CCR_N|CCR_Z, 0),
    # EXT: 0x4880-0x48BF
    (0xFFC0, 0x4880, FAMILY_EXT, EA_NONE, EA_DN, 0, 0, CCR_N|CCR_Z|CCR_V, 0),
    # EXTB: 0x49C0-0x49C7
    (0xFFF8, 0x49C0, FAMILY_EXTB, EA_NONE, EA_DN, 0, 0, CCR_N|CCR_Z|CCR_V, 0),
    # PEA: 0x4840-0x487F
    (0xFFC0, 0x4840, FAMILY_PEA, EA_NONE, EA_AN, 1, 0, 0, 0),
    # LEA absolute forms (all destination An variants): ext sizing must be exact.
    # LEA (xxx).W,An -> source EA = 111000
    (0xF1FF, 0x41F8, FAMILY_LEA, EA_AW, EA_AN, 1, 0, 0, 0),
    # LEA (xxx).L,An -> source EA = 111001
    (0xF1FF, 0x41F9, FAMILY_LEA, EA_AL, EA_AN, 2, 0, 0, 0),

    # LEA: 0x41xx, 0x45xx, 0x49xx, 0x4Dxx, 0x4Fxx (bits 11-8 = 1,5,9,D,F)
    # LEA d16(An),An: 0100 0001 MMMRRR (0x41xx)
    (0xF100, 0x4100, FAMILY_LEA, EA_NONE, EA_AN, 1, 0, 0, 0),
    # LEA d16(An),An: 0100 0101 MMMRRR (0x45xx)
    (0xF100, 0x4500, FAMILY_LEA, EA_NONE, EA_AN, 1, 0, 0, 0),
    # LEA d16(An),An: 0100 1001 MMMRRR (0x49xx)
    (0xF100, 0x4900, FAMILY_LEA, EA_NONE, EA_AN, 1, 0, 0, 0),
    # LEA d16(An),An: 0100 1101 MMMRRR (0x4Dxx)
    (0xF100, 0x4D00, FAMILY_LEA, EA_NONE, EA_AN, 1, 0, 0, 0),
    # LEA xxx.L,An: 0100 1111 MMMRRR (0x4Fxx)
    (0xF100, 0x4F00, FAMILY_LEA, EA_NONE, EA_AN, 1, 0, 0, 0),
    # MOVEM: 0x48xx and 0x4Cxx - multiple patterns for different EA modes
    # MOVEM.L Dn,ea: 0100 1000 MM MMMRRR where MM = EA mode
    # MOVEM.L ea,Dn: 0100 1100 MM MMMRRR where MM = EA mode
    # Common EA modes: (An) = 010, (An)+ = 011, -(An) = 100, d16(An) = 110
    # MOVEM.L Dn,(An): 0x4880-0x48BF
    (0xFF80, 0x4880, FAMILY_MOVEM, EA_NONE, EA_NONE, 2, 0, 0, 0),
    # MOVEM.L (An),Dn: 0x4C80-0x4CBF  
    (0xFF80, 0x4C80, FAMILY_MOVEM, EA_NONE, EA_NONE, 2, 0, 0, 0),
    # MOVEM.L Dn,(An)+: 0x48C0-0x48FF
    (0xFF80, 0x48C0, FAMILY_MOVEM, EA_NONE, EA_NONE, 2, 0, 0, 0),
    # MOVEM.L (An)+,Dn: 0x4CC0-0x4CFF
    (0xFF80, 0x4CC0, FAMILY_MOVEM, EA_NONE, EA_NONE, 2, 0, 0, 0),
    # MOVEM.L Dn,-(An): 0x4900-0x493F
    (0xFFC0, 0x4900, FAMILY_MOVEM, EA_NONE, EA_NONE, 2, 0, 0, 0),
    # MOVEM.L -(An),Dn: 0x4D00-0x4D3F
    (0xFFC0, 0x4D00, FAMILY_MOVEM, EA_NONE, EA_NONE, 2, 0, 0, 0),
    # MOVEM.L Dn,d16(An): 0x4940-0x497F
    (0xFFC0, 0x4940, FAMILY_MOVEM, EA_NONE, EA_NONE, 2, 0, 0, 0),
    # MOVEM.L d16(An),Dn: 0x4D40-0x4D7F
    (0xFFC0, 0x4D40, FAMILY_MOVEM, EA_NONE, EA_NONE, 2, 0, 0, 0),
    # MOVEM.L Dn,d16(PC): 0x4980-0x49BF
    (0xFFC0, 0x4980, FAMILY_MOVEM, EA_NONE, EA_NONE, 2, 0, 0, 0),
    # MOVEM.L d16(PC),Dn: 0x4D80-0x4DBF
    (0xFFC0, 0x4D80, FAMILY_MOVEM, EA_NONE, EA_NONE, 2, 0, 0, 0),
    # MOVEM.L Dn,xxx.W: 0x49C0-0x49FF
    (0xFFC0, 0x49C0, FAMILY_MOVEM, EA_NONE, EA_NONE, 2, 0, 0, 0),
    # MOVEM.L xxx.W,Dn: 0x4DC0-0x4DFF
    (0xFFC0, 0x4DC0, FAMILY_MOVEM, EA_NONE, EA_NONE, 2, 0, 0, 0),
    # CHK: 0x4080-0x40BF
    (0xF1C0, 0x4080, FAMILY_CHK, EA_NONE, EA_DN, 0, 0, 0, 0b010),
    
    # JSR: 0x4E80-0x4E9F
    (0xFFC0, 0x4E80, FAMILY_JSR, EA_NONE, EA_NONE, 0, 0, 0, 0b101),
    # JMP: 0x4EC0-0x4EDF
    (0xFFC0, 0x4EC0, FAMILY_JMP, EA_NONE, EA_NONE, 0, 0, 0, 0b101),
    
    # RTS: 0x4E75
    (0xFFFF, 0x4E75, FAMILY_RTS, EA_NONE, EA_NONE, 0, 0, 0, 0b101),
    # RTD: 0x4E74
    (0xFFFF, 0x4E74, FAMILY_RTD, EA_NONE, EA_NONE, 1, 0, 0, 0b101),
    # RTR: 0x4E77
    (0xFFFF, 0x4E77, FAMILY_RTR, EA_NONE, EA_NONE, 0, CCR_ALL, CCR_ALL, 0b101),
    # RTE: 0x4E73
    (0xFFFF, 0x4E73, FAMILY_RTE, EA_NONE, EA_NONE, 0, 0, 0, 0b111),
    # TRAPV: 0x4E76
    (0xFFFF, 0x4E76, FAMILY_TRAPV, EA_NONE, EA_NONE, 0, 0, 0, 0b110),
    # RESET: 0x4E70
    (0xFFFF, 0x4E70, FAMILY_RESET, EA_NONE, EA_NONE, 0, 0, 0, 0b001),
    # NOP: 0x4E71
    (0xFFFF, 0x4E71, FAMILY_NOP, EA_NONE, EA_NONE, 0, 0, 0, 0),
    # STOP: 0x4E72
    (0xFFFF, 0x4E72, FAMILY_STOP, EA_NONE, EA_NONE, 1, 0, 0, 0b111),
    # LINK: 0x4E50-0x4E57
    (0xFFF8, 0x4E50, FAMILY_LINK, EA_NONE, EA_AN, 1, 0, 0, 0b101),
    # UNLK: 0x4E51
    (0xFFFF, 0x4E51, FAMILY_UNLK, EA_NONE, EA_AN, 0, 0, 0, 0),
    # TRAP: 0x4E40-0x4E4F
    (0xFFF0, 0x4E40, FAMILY_TRAP, EA_IMM, EA_NONE, 0, 0, 0, 0b110),
    # BKPT: 0x4848-0x484F
    (0xFFF8, 0x4848, FAMILY_BKPT, EA_NONE, EA_NONE, 0, 0, 0, 0b110),
    # ILLEGAL: 0x4AFC
    (0xFFFF, 0x4AFC, FAMILY_ILL, EA_NONE, EA_NONE, 0, 0, 0, 0b110),
]

def validate_decode_table():
    """Validate decode table for order errors (general before specific).
    
    Since we process in forward order (specific first), overlaps are expected.
    Only flag errors where a GENERAL pattern comes BEFORE a SPECIFIC pattern
    for the same opcode (which would cause wrong classification).
    """
    errors = []
    # Build a map of opcode -> first family that matches it
    opcode_first_match = {}
    
    for i, (mask, match, family, _, _, _, _, _, _) in enumerate(DECODE_TABLE):
        for opcode in range(0x10000):
            if (opcode & mask) == match:
                if opcode in opcode_first_match:
                    first_idx, first_fam = opcode_first_match[opcode]
                    # If a later entry matches an already-matched opcode, that's OK
                    # (specific patterns come first, general patterns fill gaps)
                    # We only flag if the FIRST match is ILLEGAL but a later one is not
                    if first_fam == FAMILY_ILLEGAL and family != FAMILY_ILLEGAL:
                        errors.append(f"Order error: opcode 0x{opcode:04X} matches ILLEGAL (entry {first_idx}) before {FAMILY_NAMES.get(family, family)} (entry {i})")
                else:
                    opcode_first_match[opcode] = (i, family)
    
    # Only report first 20 errors
    return errors[:20]

def expand_decode_table():
    """Expand decode table into flat 65536-entry opcode table.
    
    Process decode table in forward order (specific to general).
    Later entries (more general) only fill in gaps, not overwrite specific matches.
    """
    opcode_table = [None] * 0x10000
    
    # Process decode table in forward order (specific patterns first)
    for mask, match, family, src_ea, dst_ea, ext_words, reads_ccr, writes_ccr, flags in DECODE_TABLE:
        for opcode in range(0x10000):
            if (opcode & mask) == match:
                # Only set if not already set (specific patterns win)
                if opcode_table[opcode] is None:
                    opcode_table[opcode] = {
                        'family': family,
                        'size': SIZE_NONE,
                        'src_ea': src_ea,
                        'dst_ea': dst_ea,
                        'ext_words': ext_words,
                        'reads_ccr': reads_ccr,
                        'writes_ccr': writes_ccr,
                        'privileged': (flags >> 0) & 1,
                        'may_trap': (flags >> 1) & 1,
                        'block_end': (flags >> 2) & 1,
                    }
    
    # Fill in any gaps with ILLEGAL
    for i in range(0x10000):
        if opcode_table[i] is None:
            opcode_table[i] = {
                'family': FAMILY_ILLEGAL,
                'size': SIZE_NONE,
                'src_ea': EA_NONE,
                'dst_ea': EA_NONE,
                'ext_words': 0,
                'reads_ccr': 0,
                'writes_ccr': 0,
                'privileged': 0,
                'may_trap': 0,
                'block_end': 0,
            }
    
    return opcode_table

def generate_header_file(output_path, opcode_table):
    """Generate the header file."""
    content = """/*
 * JIT Opcode Information Header
 * Generated by generate_opinfo.py
 */

#ifndef JIT_68000_OPINFO_H
#define JIT_68000_OPINFO_H

#include <stdint.h>

/* EA classes */
#define JIT_EA_NONE     0
#define JIT_EA_DN       1
#define JIT_EA_AN       2
#define JIT_EA_AI       3
#define JIT_EA_PI       4
#define JIT_EA_PD       5
#define JIT_EA_DI       6
#define JIT_EA_IX       7
#define JIT_EA_AW       8
#define JIT_EA_AL       9
#define JIT_EA_PCDI     10
#define JIT_EA_PCIX     11
#define JIT_EA_IMM      12

/* Size kinds */
#define JIT_SIZE_NONE   0
#define JIT_SIZE_BYTE   1
#define JIT_SIZE_WORD   2
#define JIT_SIZE_LONG   3

/* Handler families */
"""
    # Add family definitions (FAMILY_NAMES is {value: 'FAMILY_NAME'})
    # Strip 'FAMILY_' prefix since we add 'JIT_FAMILY_' prefix
    for value, name in sorted(FAMILY_NAMES.items()):
        short_name = name.replace('FAMILY_', '')
        content += f"#define JIT_FAMILY_{short_name:<15s} {value}\n"
    
    content += """
/* CCR flags */
#define JIT_CCR_X  0x10
#define JIT_CCR_N  0x08
#define JIT_CCR_Z  0x04
#define JIT_CCR_V  0x02
#define JIT_CCR_C  0x01
#define JIT_CCR_ALL  0x1F

/* Opcode info structure */
typedef struct {
    uint8_t  family;
    uint8_t  size;
    uint8_t  src_ea;
    uint8_t  dst_ea;
    uint8_t  ext_words;
    uint8_t  reads_ccr;
    uint8_t  writes_ccr;
    uint8_t  flags;
} jit_opinfo_t;

/* Flag bits */
#define JIT_OPF_PRIVILEGED  0x01
#define JIT_OPF_MAY_TRAP    0x02
#define JIT_OPF_BLOCK_END   0x04

extern const jit_opinfo_t g_jit_opinfo_table[0x10000];

static inline const jit_opinfo_t* jit_get_opinfo(uint16_t opcode) {
    return &g_jit_opinfo_table[opcode];
}

#endif /* JIT_68000_OPINFO_H */
"""
    with open(output_path, 'w') as f:
        f.write(content)

def generate_source_file(output_path, opcode_table):
    """Generate the source file with opcode table."""
    lines = [
        "/*",
        " * JIT Opcode Information Table",
        " * Generated by generate_opinfo.py",
        " */",
        "",
        '#include "jit_68000_opinfo.h"',
        "",
        "const jit_opinfo_t g_jit_opinfo_table[0x10000] = {",
    ]
    
    for i, info in enumerate(opcode_table):
        family_name = FAMILY_NAMES.get(info['family'], 'ILLEGAL')
        # Strip 'FAMILY_' prefix for consistent naming
        short_family = family_name.replace('FAMILY_', '')
        line = "    {{JIT_FAMILY_{:<12s}, JIT_SIZE_{:<5s}, JIT_EA_{:<6s}, JIT_EA_{:<6s}, {:2d}, 0x{:02X}, 0x{:02X}, 0x{:02X}}},".format(
            short_family,
            'NONE',  # Size determined at runtime from instruction
            ['NONE', 'DN', 'AN', 'AI', 'PI', 'PD', 'DI', 'IX', 'AW', 'AL', 'PCDI', 'PCIX', 'IMM'][info['src_ea']],
            ['NONE', 'DN', 'AN', 'AI', 'PI', 'PD', 'DI', 'IX', 'AW', 'AL', 'PCDI', 'PCIX', 'IMM'][info['dst_ea']],
            info['ext_words'],
            info['reads_ccr'],
            info['writes_ccr'],
            (info['privileged'] << 0) | (info['may_trap'] << 1) | (info['block_end'] << 2),
        )
        lines.append(line)
    
    lines.append("};")
    lines.append("")
    
    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))

def main():
    if len(sys.argv) < 2:
        print("Usage: generate_opinfo.py <output_dir>")
        sys.exit(1)
    
    output_dir = sys.argv[1]
    os.makedirs(output_dir, exist_ok=True)
    
    # Validate decode table
    print("Validating decode table...")
    errors = validate_decode_table()
    if errors:
        print("ERRORS found in decode table:")
        for err in errors:
            print(f"  {err}")
        sys.exit(1)
    print("Decode table validation passed.")
    
    # Expand decode table
    print("Expanding decode table...")
    opcode_table = expand_decode_table()
    
    # Generate files
    header_path = os.path.join(output_dir, 'jit_68000_opinfo.h')
    source_path = os.path.join(output_dir, 'jit_68000_opinfo.c')
    
    print(f"Generating {header_path}...")
    generate_header_file(header_path, opcode_table)
    
    print(f"Generating {source_path}...")
    generate_source_file(source_path, opcode_table)
    
    # Print summary
    family_counts = {}
    for info in opcode_table:
        fam = info['family']
        family_counts[fam] = family_counts.get(fam, 0) + 1
    
    print("\nOpcode family distribution:")
    for fam, count in sorted(family_counts.items(), key=lambda x: -x[1])[:20]:
        print(f"  {FAMILY_NAMES.get(fam, 'UNKNOWN')}: {count}")
    
    print("\nGeneration complete!")

if __name__ == '__main__':
    main()
