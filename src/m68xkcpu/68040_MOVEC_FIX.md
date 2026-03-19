# 68040 MOVEC and Cache Instruction Support

## Problem

The emulator was hitting illegal instruction exceptions on 68040-specific instructions:

1. **MOVEC with Address Registers** - `movec A0, VBR` was not supported (only Dn was)
2. **68040 MMU Registers** - ITT0, ITT1, DTT0, DTT1, TC, AC0-3 were not handled
3. **Cache Instructions** - PFLUSHA, CPUSHA, etc. were not recognized

## Root Cause

The MOVEC translator had three limitations:
1. Only supported data registers (Dn), not address registers (An)
2. Only handled basic control registers (VBR, SFC, DFC, CACR, PCR)
3. Didn't detect 68040 cache/MMU instructions that share the MOVEC opcode space

## Solution

### 1. Address Register Support

**Before:**
```c
uint8_t reg_num = (ext >> 12) & 7;   /* Data/Address register number */
// Only used jit_emit_load_dn() and jit_emit_store_dn()
```

**After:**
```c
uint8_t addr_reg = (ext >> 14) & 1;      /* 0 = Dn, 1 = An */
uint8_t reg_num = (ext >> 12) & 7;       /* Register number */

if (addr_reg) {
    jit_emit_load_an(ctx, AARCH64_R0, reg_num);
} else {
    jit_emit_load_dn(ctx, AARCH64_R0, reg_num);
}
```

### 2. 68040 MMU Register Support

Added handling for:
- **ITT0/ITT1** (0x80C-0x80D) - Instruction Translation Tables
- **DTT0/DTT1** (0x80E-0x80F) - Data Translation Tables  
- **TC** (0x840) - Translation Control
- **AC0-AC3** (0x850-0x853) - Access Control registers

All return 0 when read, ignore when written (we don't emulate the 68040 MMU).

### 3. Cache Instruction Detection

68040 cache instructions share the MOVEC opcode (0x4E7A/0x4E7B) but have extension word bits [15:12] = 0xF:

```c
/* Check for 68040 cache/MMU instructions (ext bits [15:12] = 0xF) */
if ((ext & 0xF000) == 0xF000) {
    /* PFLUSHA, PFLUSHP, PFLUSH, CPUSHA, etc. - NOP */
    return 0;
}
```

Instructions handled as NOPs:
- **PFLUSHA** - Flush entire cache
- **PFLUSHP** - Flush cache line
- **PFLUSH** - Flush cache entry
- **CPUSHA** - Push entire cache
- **CPUSHP** - Push cache line
- **CPUSH** - Push cache entry

## Files Modified

| File | Changes |
|------|---------|
| `src/m68xkcpu/jit_block.c` | Enhanced `jit_translate_movec()` with An support, MMU registers, cache detection |

## Testing

Build succeeds:
```
$ make
...
cc -Wall ... -o emulator.tmp ... && mv -f emulator.tmp emulator
```

## Usage

```bash
# In config file:
cpu 68040
jit m68xkcpu

# Run:
sudo PISTORM_M68XK_EXEC=1 ./emulator -c min.cfg
```

## Notes

- MMU registers return 0 because we don't emulate the 68040 memory management unit
- Cache instructions are NOPs because we don't emulate the 68040 cache
- This is safe because:
  - The JIT doesn't rely on MMU translation
  - Cache coherency is handled by the JIT cache invalidation logic
  - Performance impact is minimal for typical workloads

## Next Steps for Full 68040 Support

1. **LEA** - Load Effective Address (common in all 68k code)
2. **MOVEM** - Move Multiple Registers (stack operations)
3. **MULS/MULU** - 32x32→64 multiply
4. **DIVS/DIVU** - 64/32→32 divide
5. **Bitfield ops** (BFxxx) - 68020+ specific
