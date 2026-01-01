// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <errno.h>

#include "config_file/config_file.h"
#include "gpio/ps_protocol.h"
#include "piscsi-enums.h"
#include "piscsi.h"
#include "platforms/amiga/hunk-reloc.h"
#include "log.h"

#define BE(val) be32toh(val)
#define BE16(val) be16toh(val)

// Uncomment the line below to enable debug output
#define PISCSI_DEBUG

#ifdef PISCSI_DEBUG
#define DEBUG LOG_DEBUG
//#define DEBUG_TRIVIAL printf
#define DEBUG_TRIVIAL(...) do { if (0) LOG_DEBUG(__VA_ARGS__); } while (0)

//extern void stop_cpu_emulation(uint8_t disasm_cur);
#define stop_cpu_emulation(...)

static const char *op_type_names[4] = {
    "BYTE",
    "WORD",
    "LONGWORD",
    "MEM",
};
#else
#define DEBUG(...)
#define DEBUG_TRIVIAL(...)
#define stop_cpu_emulation(...)
#endif

#ifdef FAKESTORM
#define lseek64 lseek
#endif

extern struct emulator_config *cfg;
extern uint32_t piscsi_base;
extern unsigned int cpu_type;

struct piscsi_dev devs[8];
struct piscsi_fs filesystems[NUM_FILESYSTEMS];

uint8_t piscsi_num_fs = 0;

uint8_t piscsi_cur_drive = 0;
uint32_t piscsi_u32[4];
uint32_t piscsi_dbg[8];
uint32_t piscsi_rom_size = 0;
uint8_t *piscsi_rom_ptr;
static uint32_t piscsi_driver_dest = 0;

uint32_t rom_partitions[128];
uint32_t rom_partition_prio[128];
uint32_t rom_partition_dostype[128];
uint32_t rom_cur_partition = 0, rom_cur_fs = 0;

extern unsigned char ac_piscsi_rom[];

char partition_names[128][32];
unsigned int times_used[128];
unsigned int num_partition_names = 0;

struct hunk_info piscsi_hinfo;
struct hunk_reloc piscsi_hreloc[256];

static const char *piscsi_cpu_type_name(unsigned int cpu_type) {
    // Map 1:1 to the config file CPU order to avoid mismatches.
    static const char *cpu_names[] = {
        "invalid",
        "68000",
        "68010",
        "68EC020",
        "68020",
        "68EC030",
        "68030",
        "68EC040",
        "68LC040",
        "68040",
        "SCC68070",
    };

    if (cpu_type < (sizeof(cpu_names) / sizeof(cpu_names[0])))
        return cpu_names[cpu_type];
    return "unknown";
}

void piscsi_init() {
    for (int i = 0; i < 8; i++) {
        devs[i].fd = -1;
        devs[i].lba = 0;
        devs[i].c = devs[i].h = devs[i].s = 0;
    }

    {
        unsigned int eff_cpu = cpu_type;
        if (cfg && cfg->cpu_type)
            eff_cpu = cfg->cpu_type;
        const char *cpu_name = piscsi_cpu_type_name(eff_cpu);

        int z2_idx = cfg ? get_named_mapped_item(cfg, "z2_autoconf_fast") : -1;
        char z2_desc[64];
        if (z2_idx >= 0) {
            snprintf(z2_desc, sizeof(z2_desc), "0x%08lX+0x%X",
                     (unsigned long)cfg->map_offset[z2_idx], cfg->map_size[z2_idx]);
        } else {
            snprintf(z2_desc, sizeof(z2_desc), "none");
        }
        if (piscsi_driver_dest) {
            LOG_INFO("[PISCSI][INIT] cpu=%s autoconf_base=0x%08X z2_fast=%s rom_dest=0x%08X\n",
                   cpu_name, piscsi_base, z2_desc, piscsi_driver_dest);
        } else {
            LOG_INFO("[PISCSI][INIT] cpu=%s autoconf_base=0x%08X z2_fast=%s rom_dest=pending\n",
                   cpu_name, piscsi_base, z2_desc);
        }
    }

    if (piscsi_rom_ptr == NULL) {
        FILE *in = fopen("./platforms/amiga/piscsi/piscsi.rom", "rb");
        if (in == NULL) {
            LOG_ERROR("[PISCSI][INIT] Boot ROM open failed: ./platforms/amiga/piscsi/piscsi.rom (errno=%d)\n", errno);
            // Zero out the boot ROM offset from the autoconfig ROM.
            ac_piscsi_rom[20] = 0;
            ac_piscsi_rom[21] = 0;
            ac_piscsi_rom[22] = 0;
            ac_piscsi_rom[23] = 0;
            return;
        }
        fseek(in, 0, SEEK_END);
        piscsi_rom_size = ftell(in);
        fseek(in, 0, SEEK_SET);
        piscsi_rom_ptr = malloc(piscsi_rom_size);
        fread(piscsi_rom_ptr, piscsi_rom_size, 1, in);

        fseek(in, PISCSI_DRIVER_OFFSET, SEEK_SET);
        process_hunks(in, &piscsi_hinfo, piscsi_hreloc, PISCSI_DRIVER_OFFSET);

        fclose(in);

        LOG_INFO("[PISCSI][INIT] Boot ROM loaded: size=%u bytes driver_offset=0x%X\n", piscsi_rom_size, PISCSI_DRIVER_OFFSET);
    } else {
        LOG_INFO("[PISCSI][INIT] Boot ROM already loaded: size=%u bytes\n", piscsi_rom_size);
    }
    fflush(stdout);
}

void piscsi_shutdown() {
    LOG_INFO("[PISCSI][INIT] Shutdown: closing mapped drives and file system cache.\n");
    for (int i = 0; i < 8; i++) {
        if (devs[i].fd != -1) {
            close(devs[i].fd);
            devs[i].fd = -1;
            devs[i].block_size = 0;
        }
    }

    for (int i = 0; i < NUM_FILESYSTEMS; i++) {
        if (filesystems[i].binary_data) {
            free(filesystems[i].binary_data);
            filesystems[i].binary_data = NULL;
        }
        if (filesystems[i].fhb) {
            free(filesystems[i].fhb);
            filesystems[i].fhb = NULL;
        }
        filesystems[i].h_info.current_hunk = 0;
        filesystems[i].h_info.reloc_hunks = 0;
        filesystems[i].FS_ID = 0;
        filesystems[i].handler = 0;
    }
}

void piscsi_find_partitions(struct piscsi_dev *d) {
    int fd = d->fd;
    int cur_partition = 0;
    uint8_t tmp;
    int unit = (int)(d - devs);

    for (int i = 0; i < 16; i++) {
        if (d->pb[i]) {
            free(d->pb[i]);
            d->pb[i] = NULL;
        }
    }

    if (!d->rdb || d->rdb->rdb_PartitionList == 0) {
        DEBUG("[PISCSI][PART] Unit %d: no RDB partition list present.\n", unit);
        return;
    }

    char *block = malloc(d->block_size);

    lseek(fd, BE(d->rdb->rdb_PartitionList) * d->block_size, SEEK_SET);
next_partition:;
    read(fd, block, d->block_size);

    uint32_t first = be32toh(*((uint32_t *)&block[0]));
    if (first != PART_IDENTIFIER) {
        DEBUG("[PISCSI][PART] Unit %d: invalid partition block at RDB list head=%d; aborting.\n",
              unit, BE(d->rdb->rdb_PartitionList));
        return;
    }

    struct PartitionBlock *pb = (struct PartitionBlock *)block;
    tmp = pb->pb_DriveName[0];
    pb->pb_DriveName[tmp + 1] = 0x00;
    LOG_INFO("[PISCSI][PART] Unit %d: partition %d name=%s len=%d\n",
           unit, cur_partition, pb->pb_DriveName + 1, pb->pb_DriveName[0]);
    DEBUG("[PISCSI][PART] Unit %d: checksum=0x%.8X hostid=%d\n", unit, BE(pb->pb_ChkSum), BE(pb->pb_HostID));
    DEBUG("[PISCSI][PART] Unit %d: flags=%d (0x%.8X) devflags=%d (0x%.8X)\n",
          unit, BE(pb->pb_Flags), BE(pb->pb_Flags), BE(pb->pb_DevFlags), BE(pb->pb_DevFlags));
    d->pb[cur_partition] = pb;

    for (int i = 0; i < 128; i++) {
        if (strcmp((char *)pb->pb_DriveName + 1, partition_names[i]) == 0) {
            DEBUG("[PISCSI][PART] Unit %d: duplicate name %s; renaming to %s_%d.\n",
                  unit, pb->pb_DriveName + 1, pb->pb_DriveName + 1, times_used[i] + 1);
            times_used[i]++;
            sprintf((char *)pb->pb_DriveName + 1 + pb->pb_DriveName[0], "_%d", times_used[i]);
            pb->pb_DriveName[0] += 2;
            if (times_used[i] > 9)
                pb->pb_DriveName[0]++;
            goto partition_renamed;
        }
    }
    sprintf(partition_names[num_partition_names], "%s", pb->pb_DriveName + 1);
    num_partition_names++;

partition_renamed:
    if (d->pb[cur_partition]->pb_Next != 0xFFFFFFFF) {
        uint64_t next = be32toh(pb->pb_Next);
        block = malloc(d->block_size);
        lseek64(fd, next * d->block_size, SEEK_SET);
        cur_partition++;
        DEBUG("[PISCSI][PART] Unit %d: next partition block=%d.\n", unit, be32toh(pb->pb_Next));
        goto next_partition;
    }
    DEBUG("[PISCSI][PART] Unit %d: no more partitions.\n", unit);
    d->num_partitions = cur_partition + 1;
    d->fshd_offs = lseek64(fd, 0, SEEK_CUR);

    return;
}

int piscsi_parse_rdb(struct piscsi_dev *d) {
    int fd = d->fd;
    int i = 0;
    uint8_t *block = malloc(PISCSI_MAX_BLOCK_SIZE);
    int unit = (int)(d - devs);

    lseek(fd, 0, SEEK_SET);
    for (i = 0; i < RDB_BLOCK_LIMIT; i++) {
        read(fd, block, PISCSI_MAX_BLOCK_SIZE);
        uint32_t first = be32toh(*((uint32_t *)&block[0]));
        if (first == RDB_IDENTIFIER)
            goto rdb_found;
    }
    goto no_rdb_found;
rdb_found:;
    struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)block;
    DEBUG("[PISCSI][RDB] Unit %d: RDB found at block %d.\n", unit, i);
    d->c = be32toh(rdb->rdb_Cylinders);
    d->h = be32toh(rdb->rdb_Heads);
    d->s = be32toh(rdb->rdb_Sectors);
    d->num_partitions = 0;
    DEBUG("[PISCSI][RDB] Unit %d: first partition block=%d.\n", unit, be32toh(rdb->rdb_PartitionList));
    d->block_size = be32toh(rdb->rdb_BlockBytes);
    DEBUG("[PISCSI][RDB] Unit %d: block_size=%d bytes.\n", unit, d->block_size);
    if (d->rdb)
        free(d->rdb);
    d->rdb = rdb;
    sprintf(d->rdb->rdb_DriveInitName, "pi-scsi.device");
    return 0;

no_rdb_found:;
    if (block)
        free(block);

    return -1;
}

void piscsi_refresh_drives() {
    piscsi_num_fs = 0;

    for (int i = 0; i < NUM_FILESYSTEMS; i++) {
        if (filesystems[i].binary_data) {
            free(filesystems[i].binary_data);
            filesystems[i].binary_data = NULL;
        }
        if (filesystems[i].fhb) {
            free(filesystems[i].fhb);
            filesystems[i].fhb = NULL;
        }
        filesystems[i].h_info.current_hunk = 0;
        filesystems[i].h_info.reloc_hunks = 0;
        filesystems[i].FS_ID = 0;
        filesystems[i].handler = 0;
    }

    rom_cur_fs = 0;

    for (int i = 0; i < 128; i++) {
        memset(partition_names[i], 0x00, 32);
        times_used[i] = 0;
    }
    num_partition_names = 0;

    for (int i = 0; i < NUM_UNITS; i++) {
        if (devs[i].fd != -1) {
            piscsi_parse_rdb(&devs[i]);
            piscsi_find_partitions(&devs[i]);
            piscsi_find_filesystems(&devs[i]);
        }
    }
}

void piscsi_find_filesystems(struct piscsi_dev *d) {
    if (!d->num_partitions)
        return;

    uint8_t fs_found = 0;
    int unit = (int)(d - devs);

    uint8_t *fhb_block = malloc(d->block_size);

    lseek64(d->fd, d->fshd_offs, SEEK_SET);

    struct FileSysHeaderBlock *fhb = (struct FileSysHeaderBlock *)fhb_block;
    read(d->fd, fhb_block, d->block_size);

    while (BE(fhb->fhb_ID) == FS_IDENTIFIER) {
        char *dosID = (char *)&fhb->fhb_DosType;
#ifdef PISCSI_DEBUG
        uint16_t *fsVer = (uint16_t *)&fhb->fhb_Version;

        DEBUG("[PISCSI][FS] Unit %d: FSHD block found.\n", unit);
        DEBUG("[PISCSI][FS] Unit %d: hostid=%d next=%d summed_longs=%d\n",
              unit, BE(fhb->fhb_HostID), BE(fhb->fhb_Next), BE(fhb->fhb_SummedLongs));
        DEBUG("[PISCSI][FS] Unit %d: flags=0x%.8X dostype=%c%c%c/%d\n",
              unit, BE(fhb->fhb_Flags), dosID[0], dosID[1], dosID[2], dosID[3]);
        DEBUG("[PISCSI][FS] Unit %d: version=%d.%d\n", unit, BE16(fsVer[0]), BE16(fsVer[1]));
        DEBUG("[PISCSI][FS] Unit %d: patchflags=%d type=%d\n", unit, BE(fhb->fhb_PatchFlags), BE(fhb->fhb_Type));
        DEBUG("[PISCSI][FS] Unit %d: task=%d lock=%d\n", unit, BE(fhb->fhb_Task), BE(fhb->fhb_Lock));
        DEBUG("[PISCSI][FS] Unit %d: handler=%d stack=%d\n", unit, BE(fhb->fhb_Handler), BE(fhb->fhb_StackSize));
        DEBUG("[PISCSI][FS] Unit %d: prio=%d startup=%d (0x%.8X)\n",
              unit, BE(fhb->fhb_Priority), BE(fhb->fhb_Startup), BE(fhb->fhb_Startup));
        DEBUG("[PISCSI][FS] Unit %d: seglist_blocks=%d globalvec=%d\n",
              unit, BE(fhb->fhb_Priority), BE(fhb->fhb_Startup));
        DEBUG("[PISCSI][FS] Unit %d: filesystem_name=%s\n", unit, fhb->fhb_FileSysName + 1);
#endif

        for (int i = 0; i < NUM_FILESYSTEMS; i++) {
            if (filesystems[i].FS_ID == fhb->fhb_DosType) {
                DEBUG("[PISCSI][FS] Unit %d: filesystem %c%c%c/%d already loaded; skipping.\n",
                      unit, dosID[0], dosID[1], dosID[2], dosID[3]);
                if (BE(fhb->fhb_Next) == 0xFFFFFFFF)
                    goto fs_done;

                goto skip_fs_load_lseg;
            }
        }

        if (load_lseg(d->fd, &filesystems[piscsi_num_fs].binary_data, &filesystems[piscsi_num_fs].h_info, filesystems[piscsi_num_fs].relocs, d->block_size) != -1) {
            filesystems[piscsi_num_fs].FS_ID = fhb->fhb_DosType;
            filesystems[piscsi_num_fs].fhb = fhb;
            LOG_INFO("[PISCSI][FS] Unit %d: loaded filesystem #%d %c%c%c/%d\n",
                   unit, piscsi_num_fs + 1, dosID[0], dosID[1], dosID[2], dosID[3]);
            {
                char fs_save_filename[256];
                memset(fs_save_filename, 0x00, 256);
                sprintf(fs_save_filename, "./data/fs/%c%c%c.%d", dosID[0], dosID[1], dosID[2], dosID[3]);
                FILE *save_fs = fopen(fs_save_filename, "rb");
                if (save_fs == NULL) {
                    save_fs = fopen(fs_save_filename, "wb+");
                    if (save_fs != NULL) {
                        fwrite(filesystems[piscsi_num_fs].binary_data, filesystems[piscsi_num_fs].h_info.byte_size, 1, save_fs);
                        fclose(save_fs);
                        LOG_INFO("[PISCSI][FS] Unit %d: saved filesystem %c%c%c/%d to ./data/fs.\n",
                               unit, dosID[0], dosID[1], dosID[2], dosID[3]);
                    } else {
                        LOG_WARN("[PISCSI][FS] Unit %d: failed to save filesystem to ./data/fs (permission issue?).\n", unit);
                    }
                } else {
                    fclose(save_fs);
                }
            }
            piscsi_num_fs++;
        }

skip_fs_load_lseg:;
        fs_found++;
        lseek64(d->fd, BE(fhb->fhb_Next) * d->block_size, SEEK_SET);
        fhb_block = malloc(d->block_size);
        fhb = (struct FileSysHeaderBlock *)fhb_block;
        read(d->fd, fhb_block, d->block_size);
    }

    if (!fs_found) {
        DEBUG("[PISCSI][FS] Unit %d: no file system headers found on disk.\n", unit);
    }

fs_done:;
    if (fhb_block)
        free(fhb_block);
}

struct piscsi_dev *piscsi_get_dev(uint8_t index) {
    return &devs[index];
}

void piscsi_map_drive(char *filename, uint8_t index) {
    if (index > 7) {
        LOG_WARN("[PISCSI][INIT] Drive index %d out of range; cannot map %s.\n", index, filename);
        return;
    }

    int32_t tmp_fd = open(filename, O_RDWR);
    if (tmp_fd == -1) {
        LOG_ERROR("[PISCSI][INIT] Failed to open %s for unit %d (errno=%d).\n", filename, index, errno);
        return;
    }

    char hdfID[512];
    memset(hdfID, 0x00, 512);
    read(tmp_fd, hdfID, 512);
    hdfID[4] = '\0';
    if (strcmp(hdfID, "DOS") == 0 || strcmp(hdfID, "PFS") == 0 || strcmp(hdfID, "PDS") == 0 || strcmp(hdfID, "SFS") == 0) {
        LOG_WARN("[PISCSI][INIT] %s looks like a UAE single-partition hardfile (unsupported).\n", filename);
        LOG_WARN("[PISCSI][INIT] Use full-drive RDB images for PiSCSI. See platforms/amiga/piscsi/readme.md.\n");
        LOG_INFO("[PISCSI][INIT] If this is an empty placeholder for on-Amiga partitioning, ignore this warning.\n");
    }

    struct piscsi_dev *d = &devs[index];

    uint64_t file_size = lseek(tmp_fd, 0, SEEK_END);
    d->fs = file_size;
    d->fd = tmp_fd;
    lseek(tmp_fd, 0, SEEK_SET);
    LOG_INFO("[PISCSI][INIT] Mapped unit %d to %s size=%lu bytes.\n", index, filename, (unsigned long)file_size);

    if (piscsi_parse_rdb(d) == -1) {
        DEBUG("[PISCSI][RDB] Unit %d: no RDB found, using synthesized CHS.\n", index);
        d->h = 16;
        d->s = 63;
        d->c = (file_size / 512) / (d->s * d->h);
        d->block_size = 512;
    }
    LOG_INFO("[PISCSI][RDB] Unit %d: CHS=%d/%d/%d block_size=%d\n", index, d->c, d->h, d->s, d->block_size);

    LOG_INFO("[PISCSI][PART] Unit %d: scanning partitions.\n", index);
    piscsi_find_partitions(d);
    LOG_INFO("[PISCSI][FS] Unit %d: scanning filesystem headers.\n", index);
    piscsi_find_filesystems(d);
    LOG_INFO("[PISCSI][INIT] Unit %d: map/scan complete.\n", index);

    // Perform self-test to validate HDF integrity
    LOG_INFO("[PISCSI][SELFTEST] Unit %d: running HDF integrity validation.\n", index);
    if (!piscsi_validate_hdf(d, filename)) {
        LOG_ERROR("[PISCSI][SELFTEST] Unit %d: HDF validation failed (%s)\n", index, filename);
    } else {
        LOG_INFO("[PISCSI][SELFTEST] Unit %d: HDF validation passed (%s)\n", index, filename);
    }
}

// HDF integrity validation function
int piscsi_validate_hdf(struct piscsi_dev *d, char *filename) {
    if (!d || d->fd == -1) {
        LOG_ERROR("[PISCSI][SELFTEST] Invalid device or file descriptor\n");
        return 0;
    }

    // Test 1: Read RDB block 0 (first 512 bytes)
    uint8_t rdb_block[512];
    if (lseek(d->fd, 0, SEEK_SET) == (off_t)-1) {
        LOG_ERROR("[PISCSI][SELFTEST] Cannot seek to RDB block 0 in %s\n", filename);
        return 0;
    }

    ssize_t bytes_read = read(d->fd, rdb_block, 512);
    if (bytes_read < 512) {
        LOG_ERROR("[PISCSI][SELFTEST] Cannot read full RDB block 0 from %s (got %zd bytes)\n",
               filename, bytes_read);
        return 0;
    }

    // Verify RDB signature (should start with "RDSK")
    if (rdb_block[0] == 'R' && rdb_block[1] == 'D' && rdb_block[2] == 'S' && rdb_block[3] == 'K') {
        LOG_INFO("[PISCSI][SELFTEST] Valid RDB signature found in %s\n", filename);
    } else {
        // Not all HDFs have RDB, some are partitioned drives, so this isn't always an error
        LOG_INFO("[PISCSI][SELFTEST] No RDB signature found in %s (may be partitioned drive)\n", filename);
    }

    // Test 2: For DH0 (unit 0), try to read first partition block (usually at block 2 or offset 1024)
    if (d - devs == 0) { // This is unit 0 (DH0)
        LOG_INFO("[PISCSI][SELFTEST] Unit 0: testing DH0 boot block signature.\n");

        // Look for first partition block (usually at offset 1024 for standard Amiga HDFs)
        uint8_t boot_block[512];
        if (lseek(d->fd, 1024, SEEK_SET) == (off_t)-1) {
            LOG_ERROR("[PISCSI][SELFTEST] Cannot seek to DH0 boot block in %s\n", filename);
            return 0;
        }

        bytes_read = read(d->fd, boot_block, 512);
        if (bytes_read < 512) {
            LOG_ERROR("[PISCSI][SELFTEST] Cannot read DH0 boot block from %s (got %zd bytes)\n",
                   filename, bytes_read);
            return 0;
        }

        // Check for DOS boot block signature (starts with 0x444F5300 = "DOS\0")
        uint32_t dos_sig = (boot_block[0] << 24) | (boot_block[1] << 16) | (boot_block[2] << 8) | boot_block[3];
        if (dos_sig == 0x444F5300) {
            LOG_INFO("[PISCSI][SELFTEST] DH0 has a valid DOS boot block signature.\n");
        } else {
            LOG_INFO("[PISCSI][SELFTEST] DH0 boot block signature not DOS\\0 (sig=0x%08X)\n", dos_sig);
        }
    }

    // Test 3: Verify we can seek to end of file
    off64_t file_end = lseek64(d->fd, 0, SEEK_END);
    if (file_end == (off64_t)-1) {
        LOG_ERROR("[PISCSI][SELFTEST] Cannot seek to end of file %s\n", filename);
        return 0;
    }

    if ((uint64_t)file_end != d->fs) {
        LOG_WARN("[PISCSI][SELFTEST] File size mismatch: reported=%llu, actual=%lld\n",
               (unsigned long long)d->fs, (long long)file_end);
    }

    // Test 4: Try reading a few random blocks to verify integrity
    for (int i = 0; i < 3; i++) {
        off64_t test_offset = (i + 1) * 512 * 100; // Every 100th block for testing
        if (test_offset >= (off64_t)d->fs) {
            continue; // Skip if beyond file size
        }

        uint8_t test_block[512];
        if (lseek64(d->fd, test_offset, SEEK_SET) == (off64_t)-1) {
            LOG_ERROR("[PISCSI][SELFTEST] Cannot seek to test block at offset %lld in %s\n",
                   (long long)test_offset, filename);
            return 0;
        }

        bytes_read = read(d->fd, test_block, 512);
        if (bytes_read < 512) {
            LOG_ERROR("[PISCSI][SELFTEST] Cannot read test block at offset %lld from %s (got %zd bytes)\n",
                   (long long)test_offset, filename, bytes_read);
            return 0;
        }
    }

    return 1; // All tests passed
}

void piscsi_unmap_drive(uint8_t index) {
    if (devs[index].fd != -1) {
        DEBUG("[PISCSI][INIT] Unmapped unit %d.\n", index);
        close (devs[index].fd);
        devs[index].fd = -1;
    }
}

char *io_cmd_name(int index) {
    switch (index) {
        case CMD_INVALID: return "INVALID";
        case CMD_RESET: return "RESET";
        case CMD_READ: return "READ";
        case CMD_WRITE: return "WRITE";
        case CMD_UPDATE: return "UPDATE";
        case CMD_CLEAR: return "CLEAR";
        case CMD_STOP: return "STOP";
        case CMD_START: return "START";
        case CMD_FLUSH: return "FLUSH";
        case TD_MOTOR: return "TD_MOTOR";
        case TD_SEEK: return "SEEK";
        case TD_FORMAT: return "FORMAT";
        case TD_REMOVE: return "REMOVE";
        case TD_CHANGENUM: return "CHANGENUM";
        case TD_CHANGESTATE: return "CHANGESTATE";
        case TD_PROTSTATUS: return "PROTSTATUS";
        case TD_RAWREAD: return "RAWREAD";
        case TD_RAWWRITE: return "RAWWRITE";
        case TD_GETDRIVETYPE: return "GETDRIVETYPE";
        case TD_GETNUMTRACKS: return "GETNUMTRACKS";
        case TD_ADDCHANGEINT: return "ADDCHANGEINT";
        case TD_REMCHANGEINT: return "REMCHANGEINT";
        case TD_GETGEOMETRY: return "GETGEOMETRY";
        case TD_EJECT: return "EJECT";
        case TD_LASTCOMM: return "LASTCOMM/READ64";
        case TD_WRITE64: return "WRITE64";
        case HD_SCSICMD: return "HD_SCSICMD";
        case NSCMD_DEVICEQUERY: return "NSCMD_DEVICEQUERY";
        case NSCMD_TD_READ64: return "NSCMD_TD_READ64";
        case NSCMD_TD_WRITE64: return "NSCMD_TD_WRITE64";
        case NSCMD_TD_FORMAT64: return "NSCMD_TD_FORMAT64";

        default:
            return "[!!!PISCSI] Unhandled IO command";
    }
}

#define GETSCSINAME(a) case a: return ""#a"";
#define SCSIUNHANDLED(a) return "[!!!PISCSI] Unhandled SCSI command "#a"";

char *scsi_cmd_name(int index) {
    switch(index) {
        GETSCSINAME(SCSICMD_TEST_UNIT_READY);
        GETSCSINAME(SCSICMD_INQUIRY);
        GETSCSINAME(SCSICMD_READ_6);
        GETSCSINAME(SCSICMD_WRITE_6);
        GETSCSINAME(SCSICMD_READ_10);
        GETSCSINAME(SCSICMD_WRITE_10);
        GETSCSINAME(SCSICMD_READ_CAPACITY_10);
        GETSCSINAME(SCSICMD_MODE_SENSE_6);
        GETSCSINAME(SCSICMD_READ_DEFECT_DATA_10);
        default:
            return "[!!!PISCSI] Unhandled SCSI command";
    }
}

void print_piscsi_debug_message(int index) {
    int32_t r = 0;

    switch (index) {
        case DBG_INIT:
            DEBUG("[PISCSI][INIT] Initializing device state.\n");
            break;
        case DBG_OPENDEV:
            if (piscsi_dbg[0] != 255) {
                DEBUG("[PISCSI][DRV] OpenDevice unit=%d flags=%d (0x%.2X) err=%d\n",
                      piscsi_dbg[0], piscsi_dbg[1], piscsi_dbg[1], piscsi_dbg[2]);
            }
            break;
        case DBG_CLEANUP:
            DEBUG("[PISCSI][INIT] Driver cleanup path entered.\n");
            break;
        case DBG_CHS:
            DEBUG("[PISCSI][RDB] Reported CHS=%d/%d/%d\n", piscsi_dbg[0], piscsi_dbg[1], piscsi_dbg[2]);
            break;
        case DBG_BEGINIO:
            DEBUG("[PISCSI][IO] BeginIO cmd=%d (%s) flags=0x%X quick=%d\n",
                  piscsi_dbg[0], io_cmd_name(piscsi_dbg[0]), piscsi_dbg[1], piscsi_dbg[2]);
            break;
        case DBG_ABORTIO:
            DEBUG("[PISCSI][IO] AbortIO invoked.\n");
            break;
        case DBG_SCSICMD:
            DEBUG("[PISCSI][SCSI] cmd=0x%.2X (%s) cdb_len=%d bytes: %.2X %.2X %.2X\n",
                  piscsi_dbg[1], scsi_cmd_name(piscsi_dbg[1]), piscsi_dbg[4],
                  piscsi_dbg[1], piscsi_dbg[2], piscsi_dbg[3]);
            break;
        case DBG_SCSI_UNKNOWN_MODESENSE:
            DEBUG("[PISCSI][SCSI][WARN] Unknown MODE SENSE page=0x%.4X\n", piscsi_dbg[0]);
            break;
        case DBG_SCSI_UNKNOWN_COMMAND:
            DEBUG("[PISCSI][SCSI][WARN] Unknown SCSI command 0x%.4X\n", piscsi_dbg[0]);
            break;
        case DBG_SCSIERR:
            DEBUG("[PISCSI][SCSI][ERROR] SCSI error code=0x%.4X\n", piscsi_dbg[0]);
            break;
        case DBG_IOCMD:
            DEBUG_TRIVIAL("[PISCSI][IO] Command %d (%s)\n", piscsi_dbg[0], io_cmd_name(piscsi_dbg[0]));
            break;
        case DBG_IOCMD_UNHANDLED:
            DEBUG("[PISCSI][IO][WARN] Unhandled IO command 0x%.4X (%s)\n", piscsi_dbg[0], io_cmd_name(piscsi_dbg[0]));
            break;
        case DBG_SCSI_FORMATDEVICE:
            DEBUG("[PISCSI][SCSI] MODE SENSE: FormatDevice page requested.\n");
            break;
        case DBG_SCSI_RDG:
            DEBUG("[PISCSI][SCSI] MODE SENSE: RDG page requested.\n");
            break;
        case DBG_SCSICMD_RW10:
#ifdef PISCSI_DEBUG
            r = get_mapped_item_by_address(cfg, piscsi_dbg[0]);
            struct SCSICmd_RW10 *rwdat = NULL;
            uint8_t data[10];
            if (r != -1) {
                uint32_t addr = piscsi_dbg[0] - cfg->map_offset[r];
                rwdat = (struct SCSICmd_RW10 *)(&cfg->map_data[r][addr]);
            }
            else {
                DEBUG_TRIVIAL("[RW10] scsiData: %.8X\n", piscsi_dbg[0]);
                for (int i = 0; i < 10; i++) {
                    data[i] = read8(piscsi_dbg[0] + i);
                }
                rwdat = (struct SCSICmd_RW10 *)data;
            }
            if (rwdat) {
                DEBUG_TRIVIAL("[RW10] CMD: %.2X\n", rwdat->opcode);
                DEBUG_TRIVIAL("[RW10] RDP: %.2X\n", rwdat->rdprotect_flags);
                DEBUG_TRIVIAL("[RW10] Block: %d (%d)\n", rwdat->block, BE(rwdat->block));
                DEBUG_TRIVIAL("[RW10] Res_Group: %.2X\n", rwdat->res_groupnum);
                DEBUG_TRIVIAL("[RW10] Len: %d (%d)\n", rwdat->len, BE16(rwdat->len));
            }
#endif
            break;
        case DBG_SCSI_DEBUG_MODESENSE_6:
            DEBUG_TRIVIAL("[PISCSI][SCSI] MODE SENSE(6) CDB at 0x%.8X\n", piscsi_dbg[0]);
            r = get_mapped_item_by_address(cfg, piscsi_dbg[0]);
            if (r != -1) {
#ifdef PISCSI_DEBUG
                uint32_t addr = piscsi_dbg[0] - cfg->map_offset[r];
                struct SCSICmd_ModeSense6 *sense = (struct SCSICmd_ModeSense6 *)(&cfg->map_data[r][addr]);
                DEBUG_TRIVIAL("[PISCSI][SCSI] MODE SENSE(6) opcode=0x%.2X\n", sense->opcode);
                DEBUG_TRIVIAL("[PISCSI][SCSI] MODE SENSE(6) DBD=%d PC=%d\n", sense->reserved_dbd & 0x04, (sense->pc_pagecode & 0xC0 >> 6));
                DEBUG_TRIVIAL("[PISCSI][SCSI] MODE SENSE(6) page=0x%.2X subpage=0x%.2X\n", (sense->pc_pagecode & 0x3F), sense->subpage_code);
                DEBUG_TRIVIAL("[PISCSI][SCSI] MODE SENSE(6) alloc_len=%d control=0x%.2X\n", sense->alloc_len, sense->control);
#endif
            }
            else {
                DEBUG("[PISCSI][SCSI][WARN] MODE SENSE CDB not in mapped memory.\n");
            }
            break;
        default:
            DEBUG("[PISCSI][DBGME][WARN] Unhandled debug index %d.\n", index);
            break;
    }
}

#define DEBUGME_SIMPLE(i, s) case i: DEBUG(s); break;

void piscsi_debugme(uint32_t index) {
    switch (index) {
        DEBUGME_SIMPLE(1, "[PISCSI][DBGME] DiagEntry reached.\n");
        DEBUGME_SIMPLE(2, "[PISCSI][DBGME] BootEntry reached (unexpected path).\n");
        DEBUGME_SIMPLE(3, "[PISCSI][DBGME] Init: interrupts disabled.\n");
        DEBUGME_SIMPLE(4, "[PISCSI][DBGME] Init: copying/relocating driver.\n");
        DEBUGME_SIMPLE(5, "[PISCSI][DBGME] Init: InitResident.\n");
        DEBUGME_SIMPLE(7, "[PISCSI][DBGME] Init: begin partition loop.\n");
        DEBUGME_SIMPLE(8, "[PISCSI][DBGME] Init: partition loop done; return to Exec.\n");
        DEBUGME_SIMPLE(9, "[PISCSI][DBGME] Init: LoadFileSystems.\n");
        DEBUGME_SIMPLE(10, "[PISCSI][DBGME] Init: AllocMem for resident.\n");
        DEBUGME_SIMPLE(11, "[PISCSI][DBGME] Init: checking resident.\n");
        DEBUGME_SIMPLE(22, "[PISCSI][DBGME] BootEntry reached.\n");
        case 30:
            DEBUG("[PISCSI][DBGME] LoadFileSystems: opening FileSystem.resource.\n");
            rom_cur_fs = 0;
            break;
        DEBUGME_SIMPLE(33, "[PISCSI][DBGME] FileSystem.resource missing; creating.\n");
        case 31:
            DEBUG("[PISCSI][DBGME] OpenResource result: %d\n", piscsi_u32[0]);
            break;
        case 32:
            DEBUG("[PISCSI][DBGME] LoadFileSystems: end of FileSysEntry list.\n");
            break;
        case 35:
            DEBUG("[PISCSI][DBGME] LoadFileSystems: scanning FileSysEntry list.\n");
            break;
        case 36:
            DEBUG("[PISCSI][DBGME] Debug pointers: %.8X %.8X %.8X %.8X\n",
                  piscsi_u32[0], piscsi_u32[1], piscsi_u32[2], piscsi_u32[3]);
            break;
        default:
            // Handle undefined indexes by printing the index number
            DEBUG("[PISCSI][DBGME] Unhandled code=%u\n", index);
            break;
    }

    if (index == 8) {
        stop_cpu_emulation(1);
    }
}

void handle_piscsi_write(uint32_t addr, uint32_t val, uint8_t type) {
    int32_t r;
    uint8_t *map;
#ifndef PISCSI_DEBUG
    if (type) {}
#endif

    struct piscsi_dev *d = &devs[piscsi_cur_drive];

    uint16_t cmd = (addr & 0xFFFF);

    switch (cmd) {
        case PISCSI_CMD_READ64:
        case PISCSI_CMD_READ:
        case PISCSI_CMD_READBYTES:
            d = &devs[val];
            if (d->fd == -1) {
                DEBUG("[PISCSI][IO][ERROR] Unit %d: read requested but drive not mapped.\n", val);
                break;
            }

            if (cmd == PISCSI_CMD_READBYTES) {
                uint32_t src = piscsi_u32[0];
                uint32_t block = src / d->block_size;
                d->lba = block;
                DEBUG("[PISCSI][IO] Unit:%d READBYTES offset=0x%X len=%d LBA=0x%X file_offset=0x%X dst=0x%.8X\n",
                      val, src, piscsi_u32[1], block, src, piscsi_u32[2]);
                lseek(d->fd, src, SEEK_SET);
            }
            else if (cmd == PISCSI_CMD_READ) {
                uint32_t block = piscsi_u32[0];
                uint64_t file_offset = (uint64_t)block * d->block_size;
                d->lba = block;
                DEBUG("[PISCSI][IO] Unit:%d READ offset=0x%X len=%d LBA=0x%X file_offset=0x%llX dst=0x%.8X\n",
                      val, block, piscsi_u32[1], block, (unsigned long long)file_offset, piscsi_u32[2]);
                lseek(d->fd, file_offset, SEEK_SET);
            }
            else {
                uint64_t src = ((uint64_t)piscsi_u32[3] << 32) | piscsi_u32[0];
                uint32_t block = (uint32_t)(src / d->block_size);
                d->lba = block;
                DEBUG("[PISCSI][IO] Unit:%d READ64 offset=0x%llX len=%d LBA=0x%X file_offset=0x%llX dst=0x%.8X\n",
                      val, (unsigned long long)src, piscsi_u32[1], block, (unsigned long long)src, piscsi_u32[2]);
                lseek64(d->fd, src, SEEK_SET);
            }

            map = get_mapped_data_pointer_by_address(cfg, piscsi_u32[2]);
            if (map) {
                DEBUG_TRIVIAL("[PISCSI][IO] Unit %d: DMA read to mapped range %d.\n", val, r);
                ssize_t bytes_read = read(d->fd, map, piscsi_u32[1]);
                if (bytes_read < 0) {
                    DEBUG("[PISCSI][IO][ERROR] Unit:%d READ failed: requested=%d read=%zd errno=%d\n",
                          val, piscsi_u32[1], bytes_read, errno);
                } else if (bytes_read != (ssize_t)piscsi_u32[1]) {
                    DEBUG("[PISCSI][IO][WARN] Unit:%d partial READ: requested=%d actual=%zd\n",
                          val, piscsi_u32[1], bytes_read);
                } else {
                    DEBUG("[PISCSI][IO][OK] Unit:%d READ ok: %zd bytes\n", val, bytes_read);
                }
            }
            else {
                DEBUG_TRIVIAL("[PISCSI][IO] Unit %d: no mapped range, byte-at-a-time read.\n", val);
                uint8_t c = 0;
                int success = 1;
                for (uint32_t i = 0; i < piscsi_u32[1]; i++) {
                    ssize_t result = read(d->fd, &c, 1);
                    if (result <= 0) {
                        DEBUG("[PISCSI][IO][ERROR] Unit:%d byte READ failed at offset %d: result=%zd\n",
                              val, i, result);
                        success = 0;
                        break;
                    }
                    m68k_write_memory_8(piscsi_u32[2] + i, (uint32_t)c);
                }
                if (success) {
                    DEBUG("[PISCSI][IO][OK] Unit:%d byte READ ok: %d bytes\n", val, piscsi_u32[1]);
                }
            }
            break;
        case PISCSI_CMD_WRITE64:
        case PISCSI_CMD_WRITE:
        case PISCSI_CMD_WRITEBYTES:
            d = &devs[val];
            if (d->fd == -1) {
                DEBUG("[PISCSI][IO][ERROR] Unit %d: write requested but drive not mapped.\n", val);
                break;
            }

            if (cmd == PISCSI_CMD_WRITEBYTES) {
                uint32_t src = piscsi_u32[0];
                uint32_t block = src / d->block_size;
                d->lba = block;
                DEBUG("[PISCSI][IO] Unit:%d WRITEBYTES offset=0x%X len=%d LBA=0x%X file_offset=0x%X src=0x%.8X\n",
                      val, src, piscsi_u32[1], block, src, piscsi_u32[2]);
                lseek(d->fd, src, SEEK_SET);
            }
            else if (cmd == PISCSI_CMD_WRITE) {
                uint32_t block = piscsi_u32[0];
                uint64_t file_offset = (uint64_t)block * d->block_size;
                d->lba = block;
                DEBUG("[PISCSI][IO] Unit:%d WRITE offset=0x%X len=%d LBA=0x%X file_offset=0x%llX src=0x%.8X\n",
                      val, block, piscsi_u32[1], block, (unsigned long long)file_offset, piscsi_u32[2]);
                lseek(d->fd, file_offset, SEEK_SET);
            }
            else {
                uint64_t src = ((uint64_t)piscsi_u32[3] << 32) | piscsi_u32[0];
                uint32_t block = (uint32_t)(src / d->block_size);
                d->lba = block;
                DEBUG("[PISCSI][IO] Unit:%d WRITE64 offset=0x%llX len=%d LBA=0x%X file_offset=0x%llX src=0x%.8X\n",
                      val, (unsigned long long)src, piscsi_u32[1], block, (unsigned long long)src, piscsi_u32[2]);
                lseek64(d->fd, src, SEEK_SET);
            }

            map = get_mapped_data_pointer_by_address(cfg, piscsi_u32[2]);
            if (map) {
                DEBUG_TRIVIAL("[PISCSI][IO] Unit %d: DMA write from mapped range %d.\n", val, r);
                ssize_t bytes_written = write(d->fd, map, piscsi_u32[1]);
                if (bytes_written < 0) {
                    DEBUG("[PISCSI][IO][ERROR] Unit:%d WRITE failed: requested=%d written=%zd errno=%d\n",
                          val, piscsi_u32[1], bytes_written, errno);
                } else if (bytes_written != (ssize_t)piscsi_u32[1]) {
                    DEBUG("[PISCSI][IO][WARN] Unit:%d partial WRITE: requested=%d actual=%zd\n",
                          val, piscsi_u32[1], bytes_written);
                } else {
                    DEBUG("[PISCSI][IO][OK] Unit:%d WRITE ok: %zd bytes\n", val, bytes_written);
                }
            }
            else {
                DEBUG_TRIVIAL("[PISCSI][IO] Unit %d: no mapped range, byte-at-a-time write.\n", val);
                uint8_t c = 0;
                int success = 1;
                for (uint32_t i = 0; i < piscsi_u32[1]; i++) {
                    c = m68k_read_memory_8(piscsi_u32[2] + i);
                    ssize_t result = write(d->fd, &c, 1);
                    if (result <= 0) {
                        DEBUG("[PISCSI][IO][ERROR] Unit:%d byte WRITE failed at offset %d: result=%zd\n",
                              val, i, result);
                        success = 0;
                        break;
                    }
                }
                if (success) {
                    DEBUG("[PISCSI][IO][OK] Unit:%d byte WRITE ok: %d bytes\n", val, piscsi_u32[1]);
                }
            }
            break;
        case PISCSI_CMD_ADDR1: case PISCSI_CMD_ADDR2: case PISCSI_CMD_ADDR3: case PISCSI_CMD_ADDR4: {
            int i = ((addr & 0xFFFF) - PISCSI_CMD_ADDR1) / 4;
            piscsi_u32[i] = val;
            break;
        }
        case PISCSI_CMD_DRVNUM:
            if (val > 6) {
                piscsi_cur_drive = 255;
            }
            else {
                piscsi_cur_drive = val;
            }
            if (piscsi_cur_drive != 255) {
                DEBUG("[PISCSI][DRV] (%s) current unit set to %d (raw=%d)\n",
                      op_type_names[type], piscsi_cur_drive, val);
            }
            break;
        case PISCSI_CMD_DRVNUMX:
            piscsi_cur_drive = val;
            DEBUG("[PISCSI][DRV] DRVNUMX set to %d.\n", val);
            break;
        case PISCSI_CMD_DEBUGME:
            piscsi_debugme(val);
            break;
        case PISCSI_CMD_DRIVER:
            DEBUG("[PISCSI][ROM] Driver copy/reloc: destination=0x%.8X\n", val);
            piscsi_driver_dest = val;
            r = get_mapped_item_by_address(cfg, val);
            if (r != -1) {
                uint32_t addr = val - cfg->map_offset[r];
                uint8_t *dst_data = cfg->map_data[r];
                uint8_t cur_partition = 0;
                memcpy(dst_data + addr, piscsi_rom_ptr + PISCSI_DRIVER_OFFSET, 0x4000 - PISCSI_DRIVER_OFFSET);

                piscsi_hinfo.base_offset = val;

                reloc_hunks(piscsi_hreloc, dst_data + addr, &piscsi_hinfo);

                #define PUTNODELONG(val) *(uint32_t *)&dst_data[p_offs] = htobe32(val); p_offs += 4;
                #define PUTNODELONGBE(val) *(uint32_t *)&dst_data[p_offs] = val; p_offs += 4;

                for (int i = 0; i < 128; i++) {
                    rom_partitions[i] = 0;
                    rom_partition_prio[i] = 0;
                    rom_partition_dostype[i] = 0;
                }
                rom_cur_partition = 0;

                uint32_t data_addr = addr + 0x3F00;
                sprintf((char *)dst_data + data_addr, "pi-scsi.device");
                uint32_t addr2 = addr + 0x4000;
                for (int i = 0; i < NUM_UNITS; i++) {
                    if (devs[i].fd == -1)
                        goto skip_disk;

                    if (devs[i].num_partitions) {
                        uint32_t p_offs = addr2;
                        DEBUG("[PISCSI][ROM] Unit %d: adding %d partitions to ROM device list.\n",
                              i, devs[i].num_partitions);
                        for (uint32_t j = 0; j < devs[i].num_partitions; j++) {
                            DEBUG("[PISCSI][ROM] Unit %d: partition %d name=%s\n",
                                  i, j, devs[i].pb[j]->pb_DriveName + 1);
                            sprintf((char *)dst_data + p_offs, "%s", devs[i].pb[j]->pb_DriveName + 1);
                            p_offs += 0x20;
                            PUTNODELONG(addr2 + cfg->map_offset[r]);
                            PUTNODELONG(data_addr + cfg->map_offset[r]);
                            PUTNODELONG(i);
                            PUTNODELONG(0);
                            uint32_t nodesize = (be32toh(devs[i].pb[j]->pb_Environment[0]) + 1) * 4;
                            memcpy(dst_data + p_offs, devs[i].pb[j]->pb_Environment, nodesize);

                            struct pihd_dosnode_data *dat = (struct pihd_dosnode_data *)(&dst_data[addr2+0x20]);

                            if (BE(devs[i].pb[j]->pb_Flags) & 0x01) {
                                DEBUG("[PISCSI][ROM] Unit %d: partition %d bootable.\n", i, j);
                                rom_partition_prio[cur_partition] = BE(dat->priority);
                            }
                            else {
                                DEBUG("[PISCSI][ROM] Unit %d: partition %d not bootable.\n", i, j);
                                rom_partition_prio[cur_partition] = -128;
                            }

                            DEBUG("[PISCSI][ROM] DOSNode: name=%s device=%s\n", dst_data + addr2, dst_data + data_addr);
                            DEBUG("[PISCSI][ROM] DOSNode: unit=%d flags=%d pad1=%d\n", BE(dat->unit), BE(dat->flags), BE(dat->pad1));
                            DEBUG("[PISCSI][ROM] DOSNode: nodelen=%d blocklen=%d\n", BE(dat->node_len) * 4, BE(dat->block_len) * 4);
                            DEBUG("[PISCSI][ROM] DOSNode: heads=%d spb=%d bps=%d\n", BE(dat->surf), BE(dat->secs_per_block), BE(dat->blocks_per_track));
                            DEBUG("[PISCSI][ROM] DOSNode: reserved=%d prealloc=%d\n", BE(dat->reserved_blocks), BE(dat->pad2));
                            DEBUG("[PISCSI][ROM] DOSNode: interleave=%d buffers=%d memtype=%d\n", BE(dat->interleave), BE(dat->buffers), BE(dat->mem_type));
                            DEBUG("[PISCSI][ROM] DOSNode: lowcyl=%d highcyl=%d prio=%d\n", BE(dat->lowcyl), BE(dat->highcyl), BE(dat->priority));
                            DEBUG("[PISCSI][ROM] DOSNode: maxtransfer=0x%.8X mask=0x%.8X dostype=0x%.8X\n",
                                  BE(dat->maxtransfer), BE(dat->transfer_mask), BE(dat->dostype));

                            rom_partitions[cur_partition] = addr2 + 0x20 + cfg->map_offset[r];
                            rom_partition_dostype[cur_partition] = dat->dostype;
                            cur_partition++;
                            addr2 += 0x100;
                            p_offs = addr2;
                        }
                    }
skip_disk:;
                }
            }

            break;
        case PISCSI_CMD_NEXTPART:
            DEBUG("[PISCSI][ROM] Advance partition index %d -> %d\n", rom_cur_partition, rom_cur_partition + 1);
            rom_cur_partition++;
            break;
        case PISCSI_CMD_NEXTFS:
            DEBUG("[PISCSI][ROM] Advance filesystem index %d -> %d\n", rom_cur_fs, rom_cur_fs + 1);
            rom_cur_fs++;
            break;
        case PISCSI_CMD_COPYFS:
            DEBUG("[PISCSI][ROM] Copy filesystem %d to 0x%.8X and relocate hunks.\n", rom_cur_fs, piscsi_u32[2]);
            r = get_mapped_item_by_address(cfg, piscsi_u32[2]);
            if (r != -1) {
                uint32_t addr = piscsi_u32[2] - cfg->map_offset[r];
                memcpy(cfg->map_data[r] + addr, filesystems[rom_cur_fs].binary_data, filesystems[rom_cur_fs].h_info.byte_size);
                filesystems[rom_cur_fs].h_info.base_offset = piscsi_u32[2];
                reloc_hunks(filesystems[rom_cur_fs].relocs, cfg->map_data[r] + addr, &filesystems[rom_cur_fs].h_info);
                filesystems[rom_cur_fs].handler = piscsi_u32[2];
            }
            break;
        case PISCSI_CMD_SETFSH: {
            int i = 0;
            DEBUG("[PISCSI][ROM] Set FS handler for partition %d (DeviceNode=0x%.8X)\n", rom_cur_partition, val);
            r = get_mapped_item_by_address(cfg, val);
            if (r != -1) {
                uint32_t addr = val - cfg->map_offset[r];
                struct DeviceNode *node = (struct DeviceNode *)(cfg->map_data[r] + addr);
                char *dosID = (char *)&rom_partition_dostype[rom_cur_partition];

                DEBUG("[PISCSI][ROM] Partition DOSType=%c%c%c/%d\n", dosID[0], dosID[1], dosID[2], dosID[3]);
                // First try exact match
                for (i = 0; i < piscsi_num_fs; i++) {
                    if (rom_partition_dostype[rom_cur_partition] == filesystems[i].FS_ID) {
                        node->dn_SegList = htobe32((((filesystems[i].handler) + filesystems[i].h_info.header_size) >> 2));
                        node->dn_GlobalVec = 0xFFFFFFFF;
                        goto fs_found;
                    }
                }

                // If no exact match, try fallback mappings (e.g., DOS/3 -> DOS/1 for FastFileSystem)
                uint32_t fallback_dostype = rom_partition_dostype[rom_cur_partition];

                // Map DOS/3 (FFS International) to DOS/1 (FFS) handler since they use the same filesystem
                if (fallback_dostype == 0x444F5303) { // DOS/3
                    fallback_dostype = 0x444F5301;   // DOS/1
                    for (i = 0; i < piscsi_num_fs; i++) {
                        if (fallback_dostype == filesystems[i].FS_ID) {
                            node->dn_SegList = htobe32((((filesystems[i].handler) + filesystems[i].h_info.header_size) >> 2));
                            node->dn_GlobalVec = 0xFFFFFFFF;
                            DEBUG("[PISCSI][ROM] Fallback: mapped DOS/3 partition to DOS/1 handler.\n");
                            goto fs_found;
                        }
                    }
                }

                node->dn_GlobalVec = 0xFFFFFFFF;
                node->dn_SegList = 0;
        LOG_WARN("[PISCSI][ROM] No handler found for filesystem %c%c%c/%d\n", dosID[0], dosID[1], dosID[2], dosID[3]);
fs_found:;
                DEBUG("[PISCSI][ROM] FS handler: next=%d type=0x%.8X\n", BE(node->dn_Next), BE(node->dn_Type));
                DEBUG("[PISCSI][ROM] FS handler: task=%d lock=%d\n", BE(node->dn_Task), BE(node->dn_Lock));
                DEBUG("[PISCSI][ROM] FS handler: handler=%d stack=%d\n", BE(node->dn_Handler), BE(node->dn_StackSize));
                DEBUG("[PISCSI][ROM] FS handler: prio=%d startup=%d (0x%.8X)\n", BE(node->dn_Priority), BE(node->dn_Startup), BE(node->dn_Startup));
                DEBUG("[PISCSI][ROM] FS handler: seglist=0x%.8X globalvec=%d\n",
                      BE((uint32_t)node->dn_SegList), BE(node->dn_GlobalVec));
                DEBUG("[PISCSI][ROM] Handler for partition 0x%.8X set to FSID=0x%.8X handler=0x%.8X\n",
                      BE(node->dn_Name), filesystems[i].FS_ID, filesystems[i].handler);
            }
            break;
        }
        case PISCSI_CMD_LOADFS: {
            DEBUG("[PISCSI][FS] Attempt to load filesystem for partition %d from disk.\n", rom_cur_partition);
            r = get_mapped_item_by_address(cfg, val);
            if (r != -1) {
                char *dosID = (char *)&rom_partition_dostype[rom_cur_partition];
                filesystems[piscsi_num_fs].binary_data = NULL;
                filesystems[piscsi_num_fs].fhb = NULL;
                filesystems[piscsi_num_fs].FS_ID = rom_partition_dostype[rom_cur_partition];
                filesystems[piscsi_num_fs].handler = 0;
                if (load_fs(&filesystems[piscsi_num_fs], dosID) != -1) {
                    LOG_INFO("[PISCSI][FS] Loaded filesystem %c%c%c/%d from fs storage.\n",
                           dosID[0], dosID[1], dosID[2], dosID[3]);
                    piscsi_u32[3] = piscsi_num_fs;
                    rom_cur_fs = piscsi_num_fs;
                    piscsi_num_fs++;
                } else {
                    LOG_WARN("[PISCSI][FS] Failed to load filesystem %c%c%c/%d from fs storage.\n",
                           dosID[0], dosID[1], dosID[2], dosID[3]);
                    piscsi_u32[3] = 0xFFFFFFFF;
                }
            }
            break;
        }
        case PISCSI_DBG_VAL1: case PISCSI_DBG_VAL2: case PISCSI_DBG_VAL3: case PISCSI_DBG_VAL4:
        case PISCSI_DBG_VAL5: case PISCSI_DBG_VAL6: case PISCSI_DBG_VAL7: case PISCSI_DBG_VAL8: {
            int i = ((addr & 0xFFFF) - PISCSI_DBG_VAL1) / 4;
            piscsi_dbg[i] = val;
            break;
        }
        case PISCSI_DBG_MSG:
            print_piscsi_debug_message(val);
            break;
        default:
            DEBUG("[PISCSI][REG][WARN] Unhandled %s write to 0x%.8X: %d\n", op_type_names[type], addr, val);
            break;
    }
}

#define PIB 0x00

uint32_t handle_piscsi_read(uint32_t addr, uint8_t type) {
    if (type) {}

    if ((addr & 0xFFFF) >= PISCSI_CMD_ROM) {
        uint32_t romoffs = (addr & 0xFFFF) - PISCSI_CMD_ROM;
        if (romoffs < (piscsi_rom_size + PIB)) {
            //DEBUG("[PISCSI] %s read from Boot ROM @$%.4X (%.8X): ", op_type_names[type], romoffs, addr);
            uint32_t v = 0;
            switch (type) {
                case OP_TYPE_BYTE:
                    v = piscsi_rom_ptr[romoffs - PIB];
                    //DEBUG("%.2X\n", v);
                    break;
                case OP_TYPE_WORD:
                    v = be16toh(*((uint16_t *)&piscsi_rom_ptr[romoffs - PIB]));
                    //DEBUG("%.4X\n", v);
                    break;
                case OP_TYPE_LONGWORD:
                    v = be32toh(*((uint32_t *)&piscsi_rom_ptr[romoffs - PIB]));
                    //DEBUG("%.8X\n", v);
                    break;
            }
            return v;
        }
        return 0;
    }

    switch (addr & 0xFFFF) {
        case PISCSI_CMD_ADDR1: case PISCSI_CMD_ADDR2: case PISCSI_CMD_ADDR3: case PISCSI_CMD_ADDR4: {
            int i = ((addr & 0xFFFF) - PISCSI_CMD_ADDR1) / 4;
            return piscsi_u32[i];
            break;
        }
        case PISCSI_CMD_DRVTYPE:
            if (devs[piscsi_cur_drive].fd == -1) {
                DEBUG("[PISCSI][DRV] %s read DRVTYPE unit=%d: not attached.\n",
                      op_type_names[type], piscsi_cur_drive);
                return 0;
            }
            DEBUG("[PISCSI][DRV] %s read DRVTYPE unit=%d: attached.\n",
                  op_type_names[type], piscsi_cur_drive);
            return 1;
            break;
        case PISCSI_CMD_DRVNUM:
            return piscsi_cur_drive;
            break;
        case PISCSI_CMD_CYLS:
            DEBUG("[PISCSI][RDB] %s read CYLS unit=%d: %d\n", op_type_names[type], piscsi_cur_drive, devs[piscsi_cur_drive].c);
            return devs[piscsi_cur_drive].c;
            break;
        case PISCSI_CMD_HEADS:
            DEBUG("[PISCSI][RDB] %s read HEADS unit=%d: %d\n", op_type_names[type], piscsi_cur_drive, devs[piscsi_cur_drive].h);
            return devs[piscsi_cur_drive].h;
            break;
        case PISCSI_CMD_SECS:
            DEBUG("[PISCSI][RDB] %s read SECS unit=%d: %d\n", op_type_names[type], piscsi_cur_drive, devs[piscsi_cur_drive].s);
            return devs[piscsi_cur_drive].s;
            break;
        case PISCSI_CMD_BLOCKS: {
            uint32_t blox = devs[piscsi_cur_drive].fs / devs[piscsi_cur_drive].block_size;
            DEBUG("[PISCSI][RDB] %s read BLOCKS unit=%d: %d (fs=%llu)\n",
                  op_type_names[type], piscsi_cur_drive, blox, (unsigned long long)devs[piscsi_cur_drive].fs);
            return blox;
            break;
        }
        case PISCSI_CMD_GETPART: {
            DEBUG("[PISCSI][ROM] Get partition %d offset=0x%.8X\n", rom_cur_partition, rom_partitions[rom_cur_partition]);
            return rom_partitions[rom_cur_partition];
            break;
        }
        case PISCSI_CMD_GETPRIO:
            DEBUG("[PISCSI][ROM] Get partition %d boot priority=%d\n", rom_cur_partition, rom_partition_prio[rom_cur_partition]);
            return rom_partition_prio[rom_cur_partition];
            break;
        case PISCSI_CMD_CHECKFS:
            DEBUG("[PISCSI][FS] Get current filesystem ID=0x%.8X\n", filesystems[rom_cur_fs].FS_ID);
            return filesystems[rom_cur_fs].FS_ID;
        case PISCSI_CMD_FSSIZE:
            DEBUG("[PISCSI][FS] Get filesystem alloc size=%d\n", filesystems[rom_cur_fs].h_info.alloc_size);
            return filesystems[rom_cur_fs].h_info.alloc_size;
        case PISCSI_CMD_BLOCKSIZE:
            DEBUG("[PISCSI][RDB] Get block size unit=%d: %d\n", piscsi_cur_drive, devs[piscsi_cur_drive].block_size);
            return devs[piscsi_cur_drive].block_size;
        case PISCSI_CMD_GET_FS_INFO: {
            int i = 0;
            uint32_t val = piscsi_u32[1];
            int32_t r = get_mapped_item_by_address(cfg, val);
            if (r != -1) {
#ifdef PISCSI_DEBUG
                char *dosID = (char *)&rom_partition_dostype[rom_cur_partition];
                DEBUG("[PISCSI][FS] GET_FS_INFO partition DOSType=%c%c%c/%d\n", dosID[0], dosID[1], dosID[2], dosID[3]);
#endif
                // First try exact match
                for (i = 0; i < piscsi_num_fs; i++) {
                    if (rom_partition_dostype[rom_cur_partition] == filesystems[i].FS_ID) {
                        return 0;
                    }
                }

                // If no exact match, try fallback mappings (e.g., DOS/3 -> DOS/1 for FastFileSystem)
                uint32_t fallback_dostype = rom_partition_dostype[rom_cur_partition];

                // Map DOS/3 (FFS International) to DOS/1 (FFS) handler since they use the same filesystem
                if (fallback_dostype == 0x444F5303) { // DOS/3
                    fallback_dostype = 0x444F5301;   // DOS/1
                    for (i = 0; i < piscsi_num_fs; i++) {
                        if (fallback_dostype == filesystems[i].FS_ID) {
                            DEBUG("[PISCSI][FS] GET_FS_INFO fallback: mapped DOS/3 to DOS/1 handler.\n");
                            return 0;
                        }
                    }
                }
            }
            return 1;
        }
        default:
            DEBUG("[PISCSI][REG][WARN] Unhandled %s read from 0x%.8X\n", op_type_names[type], addr);
            break;
    }

    return 0;
}
