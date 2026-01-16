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
#include "log.h"
#include "piscsi-enums.h"
#include "piscsi.h"
#include "platforms/amiga/hunk-reloc.h"

#define BE(val) be32toh(val)
#define BE16(val) be16toh(val)

// Uncomment the line below to enable debug output
#define PISCSI_DEBUG

/* Route printf-style output through the emulator logger so --log captures it. */
#undef printf
#define printf(...) LOG_INFO(__VA_ARGS__)

#ifdef PISCSI_DEBUG
#define DEBUG(...) LOG_DEBUG(__VA_ARGS__)
#define DEBUG_TRIVIAL(...) LOG_DEBUG(__VA_ARGS__)

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

struct piscsi_dev devs[8];
struct piscsi_fs filesystems[NUM_FILESYSTEMS];

uint8_t piscsi_num_fs = 0;

uint8_t piscsi_cur_drive = 0;
uint32_t piscsi_u32[4];
uint32_t piscsi_dbg[8];
uint32_t piscsi_rom_size = 0;
uint8_t *piscsi_rom_ptr;

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

void piscsi_init() {
    for (int i = 0; i < 8; i++) {
        devs[i].fd = -1;
        devs[i].lba = 0;
        devs[i].c = devs[i].h = devs[i].s = 0;
    }

    if (piscsi_rom_ptr == NULL) {
        FILE *in = fopen("./src/platforms/amiga/piscsi/piscsi.rom", "rb");
        if (in == NULL) {
            printf("[PISCSI] Could not open PISCSI Boot ROM file for reading!\n");
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

        printf("[PISCSI] Loaded Boot ROM.\n");
    } else {
        printf("[PISCSI] Boot ROM already loaded.\n");
    }
    fflush(stdout);
}

void piscsi_shutdown() {
    printf("[PISCSI] Shutting down PiSCSI.\n");
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

    for (int i = 0; i < 16; i++) {
        if (d->pb[i]) {
            free(d->pb[i]);
            d->pb[i] = NULL;
        }
    }

    if (!d->rdb || d->rdb->rdb_PartitionList == 0) {
        DEBUG("[PISCSI] No partitions on disk.\n");
        return;
    }

    char *block = malloc(d->block_size);

    lseek(fd, BE(d->rdb->rdb_PartitionList) * d->block_size, SEEK_SET);
next_partition:;
    read(fd, block, d->block_size);

    uint32_t first = be32toh(*((uint32_t *)&block[0]));
    if (first != PART_IDENTIFIER) {
        DEBUG("Entry at block %d is not a valid partition. Aborting.\n", BE(d->rdb->rdb_PartitionList));
        return;
    }

    struct PartitionBlock *pb = (struct PartitionBlock *)block;
    tmp = pb->pb_DriveName[0];
    pb->pb_DriveName[tmp + 1] = 0x00;
    printf("[PISCSI] Partition %d: %s (%d)\n", cur_partition, pb->pb_DriveName + 1, pb->pb_DriveName[0]);
    DEBUG("Checksum: %.8X HostID: %d\n", BE(pb->pb_ChkSum), BE(pb->pb_HostID));
    DEBUG("Flags: %d (%.8X) Devflags: %d (%.8X)\n", BE(pb->pb_Flags), BE(pb->pb_Flags), BE(pb->pb_DevFlags), BE(pb->pb_DevFlags));
    d->pb[cur_partition] = pb;

    for (int i = 0; i < 128; i++) {
        if (strcmp((char *)pb->pb_DriveName + 1, partition_names[i]) == 0) {
            DEBUG("[PISCSI] Duplicate partition name %s. Temporarily renaming to %s_%d.\n", pb->pb_DriveName + 1, pb->pb_DriveName + 1, times_used[i] + 1);
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
        DEBUG("[PISCSI] Next partition at block %d.\n", be32toh(pb->pb_Next));
        goto next_partition;
    }
    DEBUG("[PISCSI] No more partitions on disk.\n");
    d->num_partitions = cur_partition + 1;
    d->fshd_offs = lseek64(fd, 0, SEEK_CUR);

    return;
}

int piscsi_parse_rdb(struct piscsi_dev *d) {
    int fd = d->fd;
    int i = 0;
    uint8_t *block = malloc(PISCSI_MAX_BLOCK_SIZE);

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
    DEBUG("[PISCSI] RDB found at block %d.\n", i);
    d->c = be32toh(rdb->rdb_Cylinders);
    d->h = be32toh(rdb->rdb_Heads);
    d->s = be32toh(rdb->rdb_Sectors);
    d->num_partitions = 0;
    DEBUG("[PISCSI] RDB - first partition at block %d.\n", be32toh(rdb->rdb_PartitionList));
    d->block_size = be32toh(rdb->rdb_BlockBytes);
    DEBUG("[PISCSI] Block size: %d. (%d)\n", be32toh(rdb->rdb_BlockBytes), d->block_size);
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

    uint8_t *fhb_block = malloc(d->block_size);

    lseek64(d->fd, d->fshd_offs, SEEK_SET);

    struct FileSysHeaderBlock *fhb = (struct FileSysHeaderBlock *)fhb_block;
    read(d->fd, fhb_block, d->block_size);

    while (BE(fhb->fhb_ID) == FS_IDENTIFIER) {
        char *dosID = (char *)&fhb->fhb_DosType;
#ifdef PISCSI_DEBUG
        uint16_t *fsVer = (uint16_t *)&fhb->fhb_Version;

        DEBUG("[FSHD] FSHD Block found.\n");
        DEBUG("[FSHD] HostID: %d Next: %d Size: %d\n", BE(fhb->fhb_HostID), BE(fhb->fhb_Next), BE(fhb->fhb_SummedLongs));
        DEBUG("[FSHD] Flags: %.8X DOSType: %c%c%c/%d\n", BE(fhb->fhb_Flags), dosID[0], dosID[1], dosID[2], dosID[3]);
        DEBUG("[FSHD] Version: %d.%d\n", BE16(fsVer[0]), BE16(fsVer[1]));
        DEBUG("[FSHD] Patchflags: %d Type: %d\n", BE(fhb->fhb_PatchFlags), BE(fhb->fhb_Type));
        DEBUG("[FSHD] Task: %d Lock: %d\n", BE(fhb->fhb_Task), BE(fhb->fhb_Lock));
        DEBUG("[FSHD] Handler: %d StackSize: %d\n", BE(fhb->fhb_Handler), BE(fhb->fhb_StackSize));
        DEBUG("[FSHD] Prio: %d Startup: %d (%.8X)\n", BE(fhb->fhb_Priority), BE(fhb->fhb_Startup), BE(fhb->fhb_Startup));
        DEBUG("[FSHD] SegListBlocks: %d GlobalVec: %d\n", BE(fhb->fhb_Priority), BE(fhb->fhb_Startup));
        DEBUG("[FSHD] FileSysName: %s\n", fhb->fhb_FileSysName + 1);
#endif

        for (int i = 0; i < NUM_FILESYSTEMS; i++) {
            if (filesystems[i].FS_ID == fhb->fhb_DosType) {
                DEBUG("[FSHD] File system %c%c%c/%d already loaded. Skipping.\n", dosID[0], dosID[1], dosID[2], dosID[3]);
                if (BE(fhb->fhb_Next) == 0xFFFFFFFF)
                    goto fs_done;

                goto skip_fs_load_lseg;
            }
        }

        if (load_lseg(d->fd, &filesystems[piscsi_num_fs].binary_data, &filesystems[piscsi_num_fs].h_info, filesystems[piscsi_num_fs].relocs, d->block_size) != -1) {
            filesystems[piscsi_num_fs].FS_ID = fhb->fhb_DosType;
            filesystems[piscsi_num_fs].fhb = fhb;
            printf("[FSHD] Loaded and set up file system %d: %c%c%c/%d\n", piscsi_num_fs + 1, dosID[0], dosID[1], dosID[2], dosID[3]);
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
                        printf("[FSHD] File system %c%c%c/%d saved to fs storage.\n", dosID[0], dosID[1], dosID[2], dosID[3]);
                    } else {
                        printf("[FSHD] Failed to save file system to fs storage. (Permission issues?)\n");
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
        DEBUG("[!!!FSHD] No file systems found on hard drive!\n");
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
        printf("[PISCSI] Drive index %d out of range.\nUnable to map file %s to drive.\n", index, filename);
        return;
    }

    int32_t tmp_fd = open(filename, O_RDWR);
    if (tmp_fd == -1) {
        printf("[PISCSI] Failed to open file %s, could not map drive %d.\n", filename, index);
        return;
    }

    char hdfID[512];
    memset(hdfID, 0x00, 512);
    read(tmp_fd, hdfID, 512);
    hdfID[4] = '\0';
    if (strcmp(hdfID, "DOS") == 0 || strcmp(hdfID, "PFS") == 0 || strcmp(hdfID, "PDS") == 0 || strcmp(hdfID, "SFS") == 0) {
        printf("[!!!PISCSI] The disk image %s is a UAE Single Partition Hardfile!\n", filename);
        printf("[!!!PISCSI] WARNING: PiSCSI does NOT support UAE Single Partition Hardfiles!\n");
        printf("[!!!PISCSI] PLEASE check the PiSCSI readme file in the GitHub repo for more information.\n");
        printf("[!!!PISCSI] If this is merely an empty or placeholder file you've created to partition and format on the Amiga, please disregard this warning message.\n");
    }

    struct piscsi_dev *d = &devs[index];

    uint64_t file_size = lseek(tmp_fd, 0, SEEK_END);
    d->fs = file_size;
    d->fd = tmp_fd;
    lseek(tmp_fd, 0, SEEK_SET);
    printf("[PISCSI] Map %d: [%s] - %lu bytes.\n", index, filename, (unsigned long)file_size);

    if (piscsi_parse_rdb(d) == -1) {
        DEBUG("[PISCSI] No RDB found on disk, making up some CHS values.\n");
        d->h = 16;
        d->s = 63;
        d->c = (file_size / 512) / (d->s * d->h);
        d->block_size = 512;
    }
    printf("[PISCSI] CHS: %d %d %d\n", d->c, d->h, d->s);

    printf ("Finding partitions.\n");
    piscsi_find_partitions(d);
    printf ("Finding file systems.\n");
    piscsi_find_filesystems(d);
    printf ("Done.\n");

    // Perform self-test to validate HDF integrity
    printf("[PISCSI-SELFTEST] Running HDF integrity validation for drive %d...\n", index);
    if (!piscsi_validate_hdf(d, filename)) {
        printf("[PISCSI-SELFTEST-ERROR] HDF validation failed for drive %d (%s)\n", index, filename);
    } else {
        printf("[PISCSI-SELFTEST-SUCCESS] HDF validation passed for drive %d (%s)\n", index, filename);
    }
}

// HDF integrity validation function
int piscsi_validate_hdf(struct piscsi_dev *d, char *filename) {
    if (!d || d->fd == -1) {
        printf("[PISCSI-SELFTEST] ERROR: Invalid device or file descriptor\n");
        return 0;
    }

    // Test 1: Read RDB block 0 (first 512 bytes)
    uint8_t rdb_block[512];
    if (lseek(d->fd, 0, SEEK_SET) == (off_t)-1) {
        printf("[PISCSI-SELFTEST] ERROR: Cannot seek to RDB block 0 in %s\n", filename);
        return 0;
    }

    ssize_t bytes_read = read(d->fd, rdb_block, 512);
    if (bytes_read < 512) {
        printf("[PISCSI-SELFTEST] ERROR: Cannot read full RDB block 0 from %s (got %zd bytes)\n", filename, bytes_read);
        return 0;
    }

    // Verify RDB signature (should start with "RDSK")
    if (rdb_block[0] == 'R' && rdb_block[1] == 'D' && rdb_block[2] == 'S' && rdb_block[3] == 'K') {
        printf("[PISCSI-SELFTEST] INFO: Valid RDB signature found in %s\n", filename);
    } else {
        // Not all HDFs have RDB, some are partitioned drives, so this isn't always an error
        printf("[PISCSI-SELFTEST] INFO: No RDB signature found in %s (may be partitioned drive)\n", filename);
    }

    // Test 2: For DH0 (unit 0), try to read first partition block (usually at block 2 or offset 1024)
    if (d - devs == 0) { // This is unit 0 (DH0)
        printf("[PISCSI-SELFTEST] Testing DH0 partition accessibility...\n");

        // Look for first partition block (usually at offset 1024 for standard Amiga HDFs)
        uint8_t boot_block[512];
        if (lseek(d->fd, 1024, SEEK_SET) == (off_t)-1) {
            printf("[PISCSI-SELFTEST] ERROR: Cannot seek to DH0 boot block in %s\n", filename);
            return 0;
        }

        bytes_read = read(d->fd, boot_block, 512);
        if (bytes_read < 512) {
            printf("[PISCSI-SELFTEST] ERROR: Cannot read DH0 boot block from %s (got %zd bytes)\n", filename, bytes_read);
            return 0;
        }

        // Check for DOS boot block signature (starts with 0x444F5300 = "DOS\0")
        uint32_t dos_sig = (boot_block[0] << 24) | (boot_block[1] << 16) | (boot_block[2] << 8) | boot_block[3];
        if (dos_sig == 0x444F5300) {
            printf("[PISCSI-SELFTEST] SUCCESS: Valid DOS boot block signature found in DH0\n");
        } else {
            printf("[PISCSI-SELFTEST] INFO: No DOS boot block signature in DH0 (signature: 0x%08X)\n", dos_sig);
        }
    }

    // Test 3: Verify we can seek to end of file
    off64_t file_end = lseek64(d->fd, 0, SEEK_END);
    if (file_end == (off64_t)-1) {
        printf("[PISCSI-SELFTEST] ERROR: Cannot seek to end of file %s\n", filename);
        return 0;
    }

    if ((uint64_t)file_end != d->fs) {
        printf("[PISCSI-SELFTEST] WARNING: File size mismatch: reported=%llu, actual=%lld\n",
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
            printf("[PISCSI-SELFTEST] ERROR: Cannot seek to test block at offset %lld in %s\n",
                   (long long)test_offset, filename);
            return 0;
        }

        bytes_read = read(d->fd, test_block, 512);
        if (bytes_read < 512) {
            printf("[PISCSI-SELFTEST] ERROR: Cannot read test block at offset %lld from %s (got %zd bytes)\n",
                   (long long)test_offset, filename, bytes_read);
            return 0;
        }
    }

    return 1; // All tests passed
}

void piscsi_unmap_drive(uint8_t index) {
    if (devs[index].fd != -1) {
        DEBUG("[PISCSI] Unmapped drive %d.\n", index);
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
            DEBUG("[PISCSI] Initializing devices.\n");
            break;
        case DBG_OPENDEV:
            if (piscsi_dbg[0] != 255) {
                DEBUG("[PISCSI] Opening device %d (%d). Flags: %d (%.2X)\n", piscsi_dbg[0], piscsi_dbg[2], piscsi_dbg[1], piscsi_dbg[1]);
            }
            break;
        case DBG_CLEANUP:
            DEBUG("[PISCSI] Cleaning up.\n");
            break;
        case DBG_CHS:
            DEBUG("[PISCSI] C/H/S: %d / %d / %d\n", piscsi_dbg[0], piscsi_dbg[1], piscsi_dbg[2]);
            break;
        case DBG_BEGINIO:
            DEBUG("[PISCSI] BeginIO: io_Command: %d (%s) - io_Flags = %d - quick: %d\n", piscsi_dbg[0], io_cmd_name(piscsi_dbg[0]), piscsi_dbg[1], piscsi_dbg[2]);
            break;
        case DBG_ABORTIO:
            DEBUG("[PISCSI] AbortIO!\n");
            break;
        case DBG_SCSICMD:
            DEBUG("[PISCSI] SCSI Command %d (%s)\n", piscsi_dbg[1], scsi_cmd_name(piscsi_dbg[1]));
            DEBUG("Len: %d - %.2X %.2X %.2X - Command Length: %d\n", piscsi_dbg[0], piscsi_dbg[1], piscsi_dbg[2], piscsi_dbg[3], piscsi_dbg[4]);
            break;
        case DBG_SCSI_UNKNOWN_MODESENSE:
            DEBUG("[!!!PISCSI] SCSI: Unknown modesense %.4X\n", piscsi_dbg[0]);
            break;
        case DBG_SCSI_UNKNOWN_COMMAND:
            DEBUG("[!!!PISCSI] SCSI: Unknown command %.4X\n", piscsi_dbg[0]);
            break;
        case DBG_SCSIERR:
            DEBUG("[!!!PISCSI] SCSI: An error occured: %.4X\n", piscsi_dbg[0]);
            break;
        case DBG_IOCMD:
            DEBUG_TRIVIAL("[PISCSI] IO Command %d (%s)\n", piscsi_dbg[0], io_cmd_name(piscsi_dbg[0]));
            break;
        case DBG_IOCMD_UNHANDLED:
            DEBUG("[!!!PISCSI] WARN: IO command %.4X (%s) is unhandled by driver.\n", piscsi_dbg[0], io_cmd_name(piscsi_dbg[0]));
            break;
        case DBG_SCSI_FORMATDEVICE:
            DEBUG("[PISCSI] Get SCSI FormatDevice MODE SENSE.\n");
            break;
        case DBG_SCSI_RDG:
            DEBUG("[PISCSI] Get SCSI RDG MODE SENSE.\n");
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
            DEBUG_TRIVIAL("[PISCSI] SCSI ModeSense debug. Data: %.8X\n", piscsi_dbg[0]);
            r = get_mapped_item_by_address(cfg, piscsi_dbg[0]);
            if (r != -1) {
#ifdef PISCSI_DEBUG
                uint32_t addr = piscsi_dbg[0] - cfg->map_offset[r];
                struct SCSICmd_ModeSense6 *sense = (struct SCSICmd_ModeSense6 *)(&cfg->map_data[r][addr]);
                DEBUG_TRIVIAL("[SenseData] CMD: %.2X\n", sense->opcode);
                DEBUG_TRIVIAL("[SenseData] DBD: %d\n", sense->reserved_dbd & 0x04);
                DEBUG_TRIVIAL("[SenseData] PC: %d\n", (sense->pc_pagecode & 0xC0 >> 6));
                DEBUG_TRIVIAL("[SenseData] PageCodes: %.2X %.2X\n", (sense->pc_pagecode & 0x3F), sense->subpage_code);
                DEBUG_TRIVIAL("[SenseData] AllocLen: %d\n", sense->alloc_len);
                DEBUG_TRIVIAL("[SenseData] Control: %.2X (%d)\n", sense->control, sense->control);
#endif
            }
            else {
                DEBUG("[!!!PISCSI] ModeSense data not immediately available.\n");
            }
            break;
        default:
            DEBUG("[!!!PISCSI] No debug message available for index %d.\n", index);
            break;
    }
}

#define DEBUGME_SIMPLE(i, s) case i: DEBUG(s); break;

void piscsi_debugme(uint32_t index) {
    switch (index) {
        DEBUGME_SIMPLE(1, "[PISCSI-DEBUGME] Arrived at DiagEntry.\n");
        DEBUGME_SIMPLE(2, "[PISCSI-DEBUGME] Arrived at BootEntry, for some reason.\n");
        DEBUGME_SIMPLE(3, "[PISCSI-DEBUGME] Init: Interrupt disable.\n");
        DEBUGME_SIMPLE(4, "[PISCSI-DEBUGME] Init: Copy/reloc driver.\n");
        DEBUGME_SIMPLE(5, "[PISCSI-DEBUGME] Init: InitResident.\n");
        DEBUGME_SIMPLE(7, "[PISCSI-DEBUGME] Init: Begin partition loop.\n");
        DEBUGME_SIMPLE(8, "[PISCSI-DEBUGME] Init: Partition loop done. Cleaning up and returning to Exec.\n");
        DEBUGME_SIMPLE(9, "[PISCSI-DEBUGME] Init: Load file systems.\n");
        DEBUGME_SIMPLE(10, "[PISCSI-DEBUGME] Init: AllocMem for resident.\n");
        DEBUGME_SIMPLE(11, "[PISCSI-DEBUGME] Init: Checking if resident is loaded.\n");
        DEBUGME_SIMPLE(22, "[PISCSI-DEBUGME] Arrived at BootEntry.\n");
        case 30:
            DEBUG("[PISCSI-DEBUGME] LoadFileSystems: Opening FileSystem.resource.\n");
            rom_cur_fs = 0;
            break;
        DEBUGME_SIMPLE(33, "[PISCSI-DEBUGME] FileSystem.resource not available, creating.\n");
        case 31:
            DEBUG("[PISCSI-DEBUGME] OpenResource result: %d\n", piscsi_u32[0]);
            break;
        case 32:
            DEBUG("AAAAHH!\n");
            break;
        case 35:
            DEBUG("[PISCSI-DEBUGME] stuff output\n");
            break;
        case 36:
            DEBUG("[PISCSI-DEBUGME] Debug pointers: %.8X %.8X %.8X %.8X\n", piscsi_u32[0], piscsi_u32[1], piscsi_u32[2], piscsi_u32[3]);
            break;
        default:
            // Handle undefined indexes by printing the index number
            DEBUG("[PISCSI-DEBUGME] idx=%u (no string)\n", index);
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
                DEBUG("[!!!PISCSI] BUG: Attempted read from unmapped drive %d.\n", val);
                break;
            }

            if (cmd == PISCSI_CMD_READBYTES) {
                uint32_t src = piscsi_u32[0];
                uint32_t block = src / d->block_size;
                d->lba = block;
                DEBUG("[PISCSI-IO] Unit:%d CMD:READBYTES io_Offset:0x%X io_Length:%d LBA:0x%X file_offset:0x%X to_addr:0x%.8X\n", val, src, piscsi_u32[1], block, src, piscsi_u32[2]);
                lseek(d->fd, src, SEEK_SET);
            }
            else if (cmd == PISCSI_CMD_READ) {
                uint32_t block = piscsi_u32[0];
                uint64_t file_offset = (uint64_t)block * d->block_size;
                d->lba = block;
                DEBUG("[PISCSI-IO] Unit:%d CMD:READ io_Offset:0x%X io_Length:%d LBA:0x%X file_offset:0x%llX to_addr:0x%.8X\n", val, block, piscsi_u32[1], block, (unsigned long long)file_offset, piscsi_u32[2]);
                lseek(d->fd, file_offset, SEEK_SET);
            }
            else {
                uint64_t src = ((uint64_t)piscsi_u32[3] << 32) | piscsi_u32[0];
                uint32_t block = (uint32_t)(src / d->block_size);
                d->lba = block;
                DEBUG("[PISCSI-IO] Unit:%d CMD:READ64 io_Offset:0x%llX io_Length:%d LBA:0x%X file_offset:0x%llX to_addr:0x%.8X\n", val, (unsigned long long)src, piscsi_u32[1], block, (unsigned long long)src, piscsi_u32[2]);
                lseek64(d->fd, src, SEEK_SET);
            }

            map = get_mapped_data_pointer_by_address(cfg, piscsi_u32[2]);
            if (map) {
                DEBUG_TRIVIAL("[PISCSI-%d] \"DMA\" Read goes to mapped range %d.\n", val, r);
                ssize_t bytes_read = read(d->fd, map, piscsi_u32[1]);
                if (bytes_read < 0) {
                    DEBUG("[PISCSI-IO-ERROR] Unit:%d READ failed: bytes_requested=%d, bytes_read=%zd, errno=%d\n", val, piscsi_u32[1], bytes_read, errno);
                } else if (bytes_read != (ssize_t)piscsi_u32[1]) {
                    DEBUG("[PISCSI-IO-WARN] Unit:%d PARTIAL READ: requested=%d, actual=%zd\n", val, piscsi_u32[1], bytes_read);
                } else {
                    DEBUG("[PISCSI-IO-SUCCESS] Unit:%d READ: %zd bytes OK\n", val, bytes_read);
                }
            }
            else {
                DEBUG_TRIVIAL("[PISCSI-%d] No mapped range found for read.\n", val);
                uint8_t c = 0;
                int success = 1;
                for (uint32_t i = 0; i < piscsi_u32[1]; i++) {
                    ssize_t result = read(d->fd, &c, 1);
                    if (result <= 0) {
                        DEBUG("[PISCSI-IO-ERROR] Unit:%d BYTE READ failed at offset %d: result=%zd\n", val, i, result);
                        success = 0;
                        break;
                    }
                    m68k_write_memory_8(piscsi_u32[2] + i, (uint32_t)c);
                }
                if (success) {
                    DEBUG("[PISCSI-IO-SUCCESS] Unit:%d BYTE READ: %d bytes OK\n", val, piscsi_u32[1]);
                }
            }
            break;
        case PISCSI_CMD_WRITE64:
        case PISCSI_CMD_WRITE:
        case PISCSI_CMD_WRITEBYTES:
            d = &devs[val];
            if (d->fd == -1) {
                DEBUG ("[PISCSI] BUG: Attempted write to unmapped drive %d.\n", val);
                break;
            }

            if (cmd == PISCSI_CMD_WRITEBYTES) {
                uint32_t src = piscsi_u32[0];
                uint32_t block = src / d->block_size;
                d->lba = block;
                DEBUG("[PISCSI-IO] Unit:%d CMD:WRITEBYTES io_Offset:0x%X io_Length:%d LBA:0x%X file_offset:0x%X from_addr:0x%.8X\n", val, src, piscsi_u32[1], block, src, piscsi_u32[2]);
                lseek(d->fd, src, SEEK_SET);
            }
            else if (cmd == PISCSI_CMD_WRITE) {
                uint32_t block = piscsi_u32[0];
                uint64_t file_offset = (uint64_t)block * d->block_size;
                d->lba = block;
                DEBUG("[PISCSI-IO] Unit:%d CMD:WRITE io_Offset:0x%X io_Length:%d LBA:0x%X file_offset:0x%llX from_addr:0x%.8X\n", val, block, piscsi_u32[1], block, (unsigned long long)file_offset, piscsi_u32[2]);
                lseek(d->fd, file_offset, SEEK_SET);
            }
            else {
                uint64_t src = ((uint64_t)piscsi_u32[3] << 32) | piscsi_u32[0];
                uint32_t block = (uint32_t)(src / d->block_size);
                d->lba = block;
                DEBUG("[PISCSI-IO] Unit:%d CMD:WRITE64 io_Offset:0x%llX io_Length:%d LBA:0x%X file_offset:0x%llX from_addr:0x%.8X\n", val, (unsigned long long)src, piscsi_u32[1], block, (unsigned long long)src, piscsi_u32[2]);
                lseek64(d->fd, src, SEEK_SET);
            }

            map = get_mapped_data_pointer_by_address(cfg, piscsi_u32[2]);
            if (map) {
                DEBUG_TRIVIAL("[PISCSI-%d] \"DMA\" Write comes from mapped range %d.\n", val, r);
                ssize_t bytes_written = write(d->fd, map, piscsi_u32[1]);
                if (bytes_written < 0) {
                    DEBUG("[PISCSI-IO-ERROR] Unit:%d WRITE failed: bytes_requested=%d, bytes_written=%zd, errno=%d\n", val, piscsi_u32[1], bytes_written, errno);
                } else if (bytes_written != (ssize_t)piscsi_u32[1]) {
                    DEBUG("[PISCSI-IO-WARN] Unit:%d PARTIAL WRITE: requested=%d, actual=%zd\n", val, piscsi_u32[1], bytes_written);
                } else {
                    DEBUG("[PISCSI-IO-SUCCESS] Unit:%d WRITE: %zd bytes OK\n", val, bytes_written);
                }
            }
            else {
                DEBUG_TRIVIAL("[PISCSI-%d] No mapped range found for write.\n", val);
                uint8_t c = 0;
                int success = 1;
                for (uint32_t i = 0; i < piscsi_u32[1]; i++) {
                    c = m68k_read_memory_8(piscsi_u32[2] + i);
                    ssize_t result = write(d->fd, &c, 1);
                    if (result <= 0) {
                        DEBUG("[PISCSI-IO-ERROR] Unit:%d BYTE WRITE failed at offset %d: result=%zd\n", val, i, result);
                        success = 0;
                        break;
                    }
                }
                if (success) {
                    DEBUG("[PISCSI-IO-SUCCESS] Unit:%d BYTE WRITE: %d bytes OK\n", val, piscsi_u32[1]);
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
                DEBUG("[PISCSI] (%s) Drive number set to %d (%d)\n", op_type_names[type], piscsi_cur_drive, val);
            }
            break;
        case PISCSI_CMD_DRVNUMX:
            piscsi_cur_drive = val;
            DEBUG("[PISCSI] DRVNUMX: %d.\n", val);
            break;
        case PISCSI_CMD_DEBUGME:
            piscsi_debugme(val);
            break;
        case PISCSI_CMD_DRIVER:
            DEBUG("[PISCSI] Driver copy/patch called, destination address %.8X.\n", val);
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
                        DEBUG("[PISCSI] Adding %d partitions for unit %d\n", devs[i].num_partitions, i);
                        for (uint32_t j = 0; j < devs[i].num_partitions; j++) {
                            DEBUG("Partition %d: %s\n", j, devs[i].pb[j]->pb_DriveName + 1);
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
                                DEBUG("Partition is bootable.\n");
                                rom_partition_prio[cur_partition] = BE(dat->priority);
                            }
                            else {
                                DEBUG("Partition is not bootable.\n");
                                rom_partition_prio[cur_partition] = -128;
                            }

                            DEBUG("DOSNode Data:\n");
                            DEBUG("Name: %s Device: %s\n", dst_data + addr2, dst_data + data_addr);
                            DEBUG("Unit: %d Flags: %d Pad1: %d\n", BE(dat->unit), BE(dat->flags), BE(dat->pad1));
                            DEBUG("Node len: %d Block len: %d\n", BE(dat->node_len) * 4, BE(dat->block_len) * 4);
                            DEBUG("H: %d SPB: %d BPS: %d\n", BE(dat->surf), BE(dat->secs_per_block), BE(dat->blocks_per_track));
                            DEBUG("Reserved: %d Prealloc: %d\n", BE(dat->reserved_blocks), BE(dat->pad2));
                            DEBUG("Interleaved: %d Buffers: %d Memtype: %d\n", BE(dat->interleave), BE(dat->buffers), BE(dat->mem_type));
                            DEBUG("Lowcyl: %d Highcyl: %d Prio: %d\n", BE(dat->lowcyl), BE(dat->highcyl), BE(dat->priority));
                            DEBUG("Maxtransfer: %.8X Mask: %.8X\n", BE(dat->maxtransfer), BE(dat->transfer_mask));
                            DEBUG("DOSType: %.8X\n", BE(dat->dostype));

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
            DEBUG("[PISCSI] Switch partition %d -> %d\n", rom_cur_partition, rom_cur_partition + 1);
            rom_cur_partition++;
            break;
        case PISCSI_CMD_NEXTFS:
            DEBUG("[PISCSI] Switch file file system %d -> %d\n", rom_cur_fs, rom_cur_fs + 1);
            rom_cur_fs++;
            break;
        case PISCSI_CMD_COPYFS:
            DEBUG("[PISCSI] Copy file system %d to %.8X and reloc.\n", rom_cur_fs, piscsi_u32[2]);
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
            DEBUG("[PISCSI] Set handler for partition %d (DeviceNode: %.8X)\n", rom_cur_partition, val);
            r = get_mapped_item_by_address(cfg, val);
            if (r != -1) {
                uint32_t addr = val - cfg->map_offset[r];
                struct DeviceNode *node = (struct DeviceNode *)(cfg->map_data[r] + addr);
                char *dosID = (char *)&rom_partition_dostype[rom_cur_partition];

                DEBUG("[PISCSI] Partition DOSType is %c%c%c/%d\n", dosID[0], dosID[1], dosID[2], dosID[3]);
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
                            DEBUG("[PISCSI] Fallback: Mapped DOS/3 partition to DOS/1 filesystem handler.\n");
                            goto fs_found;
                        }
                    }
                }

                node->dn_GlobalVec = 0xFFFFFFFF;
                node->dn_SegList = 0;
                printf("[!!!PISCSI] Found no handler for file system %c%c%c/%d\n", dosID[0], dosID[1], dosID[2], dosID[3]);
fs_found:;
                DEBUG("[FS-HANDLER] Next: %d Type: %.8X\n", BE(node->dn_Next), BE(node->dn_Type));
                DEBUG("[FS-HANDLER] Task: %d Lock: %d\n", BE(node->dn_Task), BE(node->dn_Lock));
                DEBUG("[FS-HANDLER] Handler: %d Stacksize: %d\n", BE(node->dn_Handler), BE(node->dn_StackSize));
                DEBUG("[FS-HANDLER] Priority: %d Startup: %d (%.8X)\n", BE(node->dn_Priority), BE(node->dn_Startup), BE(node->dn_Startup));
                DEBUG("[FS-HANDLER] SegList: %.8X GlobalVec: %d\n", BE((uint32_t)node->dn_SegList), BE(node->dn_GlobalVec));
                DEBUG("[PISCSI] Handler for partition %.8X set to %.8X (%.8X).\n", BE(node->dn_Name), filesystems[i].FS_ID, filesystems[i].handler);
            }
            break;
        }
        case PISCSI_CMD_LOADFS: {
            DEBUG("[PISCSI] Attempt to load file system for partition %d from disk.\n", rom_cur_partition);
            r = get_mapped_item_by_address(cfg, val);
            if (r != -1) {
                char *dosID = (char *)&rom_partition_dostype[rom_cur_partition];
                filesystems[piscsi_num_fs].binary_data = NULL;
                filesystems[piscsi_num_fs].fhb = NULL;
                filesystems[piscsi_num_fs].FS_ID = rom_partition_dostype[rom_cur_partition];
                filesystems[piscsi_num_fs].handler = 0;
                if (load_fs(&filesystems[piscsi_num_fs], dosID) != -1) {
                    printf("[FSHD-Late] Loaded file system %c%c%c/%d from fs storage.\n", dosID[0], dosID[1], dosID[2], dosID[3]);
                    piscsi_u32[3] = piscsi_num_fs;
                    rom_cur_fs = piscsi_num_fs;
                    piscsi_num_fs++;
                } else {
                    printf("[FSHD-Late] Failed to load file system %c%c%c/%d from fs storage.\n", dosID[0], dosID[1], dosID[2], dosID[3]);
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
            DEBUG("[!!!PISCSI] WARN: Unhandled %s register write to %.8X: %d\n", op_type_names[type], addr, val);
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
                DEBUG("[PISCSI] %s Read from DRVTYPE %d, drive not attached.\n", op_type_names[type], piscsi_cur_drive);
                return 0;
            }
            DEBUG("[PISCSI] %s Read from DRVTYPE %d, drive attached.\n", op_type_names[type], piscsi_cur_drive);
            return 1;
            break;
        case PISCSI_CMD_DRVNUM:
            return piscsi_cur_drive;
            break;
        case PISCSI_CMD_CYLS:
            DEBUG("[PISCSI] %s Read from CYLS %d: %d\n", op_type_names[type], piscsi_cur_drive, devs[piscsi_cur_drive].c);
            return devs[piscsi_cur_drive].c;
            break;
        case PISCSI_CMD_HEADS:
            DEBUG("[PISCSI] %s Read from HEADS %d: %d\n", op_type_names[type], piscsi_cur_drive, devs[piscsi_cur_drive].h);
            return devs[piscsi_cur_drive].h;
            break;
        case PISCSI_CMD_SECS:
            DEBUG("[PISCSI] %s Read from SECS %d: %d\n", op_type_names[type], piscsi_cur_drive, devs[piscsi_cur_drive].s);
            return devs[piscsi_cur_drive].s;
            break;
        case PISCSI_CMD_BLOCKS: {
            uint32_t blox = devs[piscsi_cur_drive].fs / devs[piscsi_cur_drive].block_size;
            DEBUG("[PISCSI] %s Read from BLOCKS %d: %d\n", op_type_names[type], piscsi_cur_drive, (uint32_t)(devs[piscsi_cur_drive].fs / devs[piscsi_cur_drive].block_size));
            DEBUG("fs: %llu (%d)\n", (unsigned long long)devs[piscsi_cur_drive].fs, blox);
            return blox;
            break;
        }
        case PISCSI_CMD_GETPART: {
            DEBUG("[PISCSI] Get ROM partition %d offset: %.8X\n", rom_cur_partition, rom_partitions[rom_cur_partition]);
            return rom_partitions[rom_cur_partition];
            break;
        }
        case PISCSI_CMD_GETPRIO:
            DEBUG("[PISCSI] Get partition %d boot priority: %d\n", rom_cur_partition, rom_partition_prio[rom_cur_partition]);
            return rom_partition_prio[rom_cur_partition];
            break;
        case PISCSI_CMD_CHECKFS:
            DEBUG("[PISCSI] Get current loaded file system: %.8X\n", filesystems[rom_cur_fs].FS_ID);
            return filesystems[rom_cur_fs].FS_ID;
        case PISCSI_CMD_FSSIZE:
            DEBUG("[PISCSI] Get alloc size of loaded file system: %d\n", filesystems[rom_cur_fs].h_info.alloc_size);
            return filesystems[rom_cur_fs].h_info.alloc_size;
        case PISCSI_CMD_BLOCKSIZE:
            DEBUG("[PISCSI] Get block size of drive %d: %d\n", piscsi_cur_drive, devs[piscsi_cur_drive].block_size);
            return devs[piscsi_cur_drive].block_size;
        case PISCSI_CMD_GET_FS_INFO: {
            int i = 0;
            uint32_t val = piscsi_u32[1];
            int32_t r = get_mapped_item_by_address(cfg, val);
            if (r != -1) {
#ifdef PISCSI_DEBUG
                char *dosID = (char *)&rom_partition_dostype[rom_cur_partition];
                DEBUG("[PISCSI-GET-FS-INFO] Partition DOSType is %c%c%c/%d\n", dosID[0], dosID[1], dosID[2], dosID[3]);
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
                            DEBUG("[PISCSI-GET-FS-INFO] Fallback: Mapped DOS/3 partition to DOS/1 filesystem handler.\n");
                            return 0;
                        }
                    }
                }
            }
            return 1;
        }
        default:
            DEBUG("[!!!PISCSI] WARN: Unhandled %s register read from %.8X\n", op_type_names[type], addr);
            break;
    }

    return 0;
}
