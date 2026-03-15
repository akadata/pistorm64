#!/usr/bin/env python3
"""
Validate opcode family mapping by dumping representative samples.
"""

import json
import sys
sys.path.insert(0, 'src/m68xkcpu')
from generate_opinfo import get_family_from_opcode

# Family name mapping (reverse of FAMILY_ constants)
FAMILY_NAMES = {
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

def validate_families():
    # Load opcode data
    with open('third_party/ProcessorTests/680x0/map/68000.official.json', 'r') as f:
        opcode_map = json.load(f)
    
    # Key families to validate
    key_families = [
        'MOVEQ', 'ADDQ', 'SUBQ', 'BRA', 'SWAP', 'UNLK', 'STOP',
        'LINE_A', 'LINE_F', 'ORI_CCR', 'ANDI_CCR', 'EORI_CCR',
        'ORI_SR', 'ANDI_SR', 'EORI_SR', 'MOVE_CCR', 'MOVE_SR'
    ]
    
    # Collect samples for each family
    family_samples = {fam: [] for fam in key_families}
    
    for opcode_str, desc in opcode_map.items():
        opcode = int(opcode_str, 16)
        fam_id = get_family_from_opcode(opcode, desc)
        fam_name = FAMILY_NAMES.get(fam_id, 'UNKNOWN')
        
        if fam_name in key_families and len(family_samples[fam_name]) < 8:
            family_samples[fam_name].append((opcode, desc))
    
    # Print results
    print("=" * 80)
    print("FAMILY VALIDATION REPORT")
    print("=" * 80)
    print()
    
    for fam in key_families:
        samples = family_samples[fam]
        count = len(samples)
        status = "✓ FOUND" if count > 0 else "✗ MISSING"
        print(f"{status}: JIT_FAMILY_{fam}")
        
        if samples:
            print(f"  Sample opcodes ({count} shown):")
            for opcode, desc in samples:
                print(f"    0x{opcode:04X}  {desc}")
        print()
    
    # Summary
    print("=" * 80)
    missing = [f for f in key_families if len(family_samples[f]) == 0]
    if missing:
        print(f"MISSING FAMILIES ({len(missing)}): {', '.join(missing)}")
    else:
        print("ALL KEY FAMILIES ARE CORRECTLY CLASSIFIED!")
    print("=" * 80)

if __name__ == '__main__':
    validate_families()
