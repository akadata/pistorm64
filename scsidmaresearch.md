# Z3660 SCSI Driver DMA Research Document

## Overview
This document analyzes the differences between the PiSCSI Amiga driver and the Z3660 SCSI driver, focusing on the DMA implementation aspects that could be leveraged for the PiStorm64 project.

## Key Differences Identified

### 1. Hardware Detection and Initialization
- **PiSCSI**: Direct register mapping to PISCSI_OFFSET
- **Z3660**: Uses expansion.library to find Z3660 hardware via FindConfigDev() with vendor ID 0x144B and product ID 0x1
- **Z3660_REGS variable**: Dynamically set to the board address found by FindConfigDev()

### 2. Memory Management and DMA Buffers
- **Z3660 uses a dedicated DMA buffer region**:
  ```c
  // Uses memory at Z3660_REGS + 0x80000 as a DMA buffer
  if((ULONG)data<0x08000000)
      memcpy((uint8_t *)(Z3660_REGS + 0x80000), data, len);
  ```
- **Cache management functions**:
  ```c
  #define WRITE_CMD(COMMAND,UNIT,DATA,LEN)  do{               \
              uint32_t len2=LEN;                              \
              CachePreDMA((APTR)(DATA),&len2,0);              \
              WRITELONG(COMMAND, UNIT);                       \
              CachePostDMA((APTR)DATA,&len2,0);               \
              }while(0)
  ```

### 3. DMA Operations
- **Z3660 implements explicit DMA detection**:
  ```c
  WRITE_CMD(PISCSI_CMD_READ64,unit_num,data,len);
  ULONG dma;
  READLONG(PISCSI_CMD_USED_DMA,dma);
  if(dma!=0)
      memcpy((uint8_t *)data,(uint8_t *)(Z3660_REGS + 0x80000), len);
  ```

- **For read operations**: Data is copied from the DMA buffer to the target location if DMA was used
- **For write operations**: Data is copied to the DMA buffer before sending the command

### 4. Address Handling
- **Z3660 uses 64-bit aware commands**:
  - `TD_READ64`, `NSCMD_TD_READ64`, `TD_WRITE64`, `NSCMD_TD_WRITE64`
  - Separate address registers for 64-bit operations:
    - `PISCSI_CMD_READ_ADDR1`, `PISCSI_CMD_READ_ADDR2`, `PISCSI_CMD_READ_ADDR3`, `PISCSI_CMD_READ_ADDR4`
    - Similar sets for write operations

### 5. Unit Number Restrictions
- **Z3660 restricts certain unit numbers**:
  ```c
  if(i>=8 && i<10)
      continue;
  // and in open function:
  if (iotd && unit_num < NUM_UNITS && (unit_num<8 || unit_num>=10)) {
  ```

### 6. Block Size and Blocks Retrieval
- **Z3660 has per-unit block size and block count retrieval**:
  ```c
  uint32_t get_blocksize(uint8_t unit_num)
  uint32_t get_blocks(uint8_t unit_num)
  ```
  Rather than a single global value.

## DMA Implementation Strategy

### Memory Copy Operations
The Z3660 driver implements a dual-path approach:
1. **Direct memory access**: For memory above 0x08000000 (likely chip RAM or specially allocated memory)
2. **Buffered access**: For memory below 0x08000000, copying to/from a dedicated DMA buffer at Z3660_REGS + 0x80000

### Cache Coherency
- Uses `CachePreDMA()` and `CachePostDMA()` functions to handle cache coherency
- These functions are AmigaOS-specific and ensure data consistency between CPU cache and physical memory

### DMA Detection
- The hardware reports whether DMA was used via `PISCSI_CMD_USED_DMA`
- If DMA was used, the driver copies data from the DMA buffer back to the original location for read operations

## Potential Application to PiStorm64

### 1. DMA Buffer Strategy
- Implement a similar DMA buffer approach for memory not directly accessible by the Pi
- Use a dedicated memory region for DMA transfers

### 2. Cache Management
- Integrate cache management functions to ensure data consistency
- Handle the difference between cached and uncached memory regions

### 3. Address Translation
- Implement 64-bit aware addressing for large storage devices
- Use multiple address registers for extended addressing

### 4. Memory Classification
- Determine which memory regions can be accessed directly vs. requiring buffered transfers
- Use memory attributes to guide the transfer strategy

## Conclusion

The Z3660 SCSI driver implements a sophisticated DMA system that handles cache coherency and memory access limitations. The key insight is the use of a dedicated DMA buffer and cache management functions to ensure reliable data transfer between Amiga memory and the SCSI controller. This approach could be adapted for PiStorm64 to enable efficient DMA operations while handling the complexities of ARM/Amiga memory interoperation.

The implementation shows careful attention to memory management and hardware interaction patterns that would be valuable for improving PiSCSI performance and compatibility.