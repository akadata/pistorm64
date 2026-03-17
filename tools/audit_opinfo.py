#!/usr/bin/env python3
"""
Audit script for opcode metadata generator.

Identifies placeholder metadata issues in generated opinfo table.
"""

import json
import sys
sys.path.insert(0, 'src/m68xkcpu')
from generate_opinfo import get_family_from_opcode, FAMILY_NAMES, parse_instruction_desc

# Load opcode data
with open('third_party/ProcessorTests/680x0/map/68000.official.json') as f:
    opcode_map = json.load(f)

print("=" * 80)
print("OPCODE METADATA AUDIT REPORT")
print("=" * 80)
print()

# Statistics
total_opcodes = len(opcode_map)
placeholder_count = 0
misclassified = []
missing_ea = []
missing_ext = []
missing_ccr = []
missing_flags = []

# Hot boot families to prioritize (excluding ILLEGAL which is expected)
HOT_FAMILIES = {
    'LEA', 'ADD', 'ADDQ', 'BCC', 'BRA', 'BSR',
    'MOVE', 'MOVEQ', 'SUB', 'SUBQ', 'CMP', 'CMPM',
    'ORI', 'ANDI', 'EORI', 'ORI_CCR', 'ANDI_CCR', 'EORI_CCR',
    'ORI_SR', 'ANDI_SR', 'EORI_SR',
    'SWAP', 'UNLK', 'STOP', 'RTS', 'RTE', 'RTR',
    'DBCC', 'SCC', 'TST', 'CLR', 'NEG', 'NOT',
    'LINE_A', 'LINE_F'
}

hot_family_stats = {f: {'total': 0, 'placeholder': 0} for f in HOT_FAMILIES}

for opcode_str, desc in opcode_map.items():
    opcode = int(opcode_str, 16)
    info = parse_instruction_desc(desc, opcode)
    
    family_name = FAMILY_NAMES.get(info['family'], 'UNKNOWN')
    
    # Check for placeholder metadata
    is_placeholder = False
    issues = []
    
    # EA_NONE for both src and dst when instruction should have EAs
    if info['src_ea'] == 0 and info['dst_ea'] == 0:
        # Some instructions legitimately have no EAs
        if family_name not in ('NOP', 'RTS', 'RTE', 'RTR', 'RTD', 'RESET', 
                               'SWAP', 'EXT', 'EXTB', 'UNLK', 'STOP', 'TRAPV',
                               'LINE_A', 'LINE_F', 'ILL', 'ILLEGAL', 'BKPT',
                               'TRAP', 'CALLM', 'RTM', 'CP', 'CAS', 'CAS2',
                               'CHK2', 'CMP2', 'DIVL', 'MULL', 'PACK', 'UNPK', 'BF'):
            is_placeholder = True
            issues.append('EA_NONE/EA_NONE')
            if family_name not in ('ILLEGAL', 'ILL'):  # Don't report ILLEGAL as missing
                missing_ea.append((opcode_str, family_name, desc))
    
    # ext_words = 0 when instruction should have extension words
    if info['ext_words'] == 0:
        if family_name in ('LEA', 'MOVE', 'ADD', 'SUB', 'CMP', 'AND', 'OR', 'EOR',
                          'ADDI', 'SUBI', 'CMPI', 'ANDI', 'ORI', 'EORI',
                          'BRA', 'BCC', 'BSR', 'DBCC', 'LINK', 'RTD', 'TRAP',
                          'MOVEM', 'MOVEP', 'BF'):
            is_placeholder = True
            issues.append('ext_words=0')
            missing_ext.append((opcode_str, family_name, desc))
    
    # CCR effects not set when they should be
    if info['reads_ccr'] == 0 and info['writes_ccr'] == 0:
        if family_name in ('ADD', 'ADDQ', 'SUB', 'SUBQ', 'CMP', 'CMPM',
                          'AND', 'ANDI', 'OR', 'ORI', 'EOR', 'EORI',
                          'TST', 'CLR', 'NEG', 'NOT', 'ABC', 'NEGX',
                          'ASL', 'ASR', 'LSL', 'LSR', 'ROL', 'ROR', 'ROXL', 'ROXR'):
            is_placeholder = True
            issues.append('CCR=0/0')
            missing_ccr.append((opcode_str, family_name, desc))
    
    # Flags not set when they should be
    if info['privileged'] == 0 and info['may_trap'] == 0 and info['block_end'] == 0:
        if family_name in ('STOP', 'RTE', 'RESET', 'MOVEC', 'MOVE_SR', 'MOVE_USP',
                          'ORI_SR', 'ANDI_SR', 'EORI_SR',
                          'TRAP', 'TRAPV', 'BRA', 'BCC', 'BSR', 'JMP', 'JSR',
                          'RTS', 'RTR', 'RTD', 'LINE_A', 'LINE_F'):
            is_placeholder = True
            issues.append('flags=0')
            missing_flags.append((opcode_str, family_name, desc))
    
    if is_placeholder and family_name not in ('ILLEGAL', 'ILL'):
        placeholder_count += 1
        if family_name in HOT_FAMILIES:
            hot_family_stats[family_name]['placeholder'] += 1
    
    # Track hot family stats (exclude ILLEGAL from counting)
    if family_name in HOT_FAMILIES and family_name not in ('ILLEGAL', 'ILL'):
        hot_family_stats[family_name]['total'] += 1

# Print summary
print(f"Total opcodes: {total_opcodes}")
print(f"Opcodes with placeholder metadata: {placeholder_count} ({placeholder_count*100/total_opcodes:.1f}%)")
print()

print("HOT FAMILIES STATUS (boot-critical):")
print("-" * 80)
for family in sorted(HOT_FAMILIES):
    stats = hot_family_stats[family]
    if stats['total'] > 0:
        pct = stats['placeholder'] * 100 / stats['total']
        status = "⚠️" if pct > 50 else "✓" if pct == 0 else "~"
        print(f"  {status} {family:15s}: {stats['total']:4d} total, {stats['placeholder']:4d} placeholder ({pct:.0f}%)")
print()

print("MISSING EA CLASSIFICATION (sample):")
print("-" * 80)
for op, fam, desc in missing_ea[:20]:
    print(f"  0x{op}: {fam:15s} - {desc}")
if len(missing_ea) > 20:
    print(f"  ... and {len(missing_ea) - 20} more")
print()

print("MISSING EXTENSION WORDS (sample):")
print("-" * 80)
for op, fam, desc in missing_ext[:20]:
    print(f"  0x{op}: {fam:15s} - {desc}")
if len(missing_ext) > 20:
    print(f"  ... and {len(missing_ext) - 20} more")
print()

print("MISSING CCR EFFECTS (sample):")
print("-" * 80)
for op, fam, desc in missing_ccr[:20]:
    print(f"  0x{op}: {fam:15s} - {desc}")
if len(missing_ccr) > 20:
    print(f"  ... and {len(missing_ccr) - 20} more")
print()

print("MISSING FLAGS (sample):")
print("-" * 80)
for op, fam, desc in missing_flags[:20]:
    print(f"  0x{op}: {fam:15s} - {desc}")
if len(missing_flags) > 20:
    print(f"  ... and {len(missing_flags) - 20} more")
print()

print("=" * 80)
print("AUDIT COMPLETE")
print("=" * 80)
