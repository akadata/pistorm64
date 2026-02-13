// SPDX-License-Identifier: MIT

#ifndef PISTORM_AMIGA_FSID_H
#define PISTORM_AMIGA_FSID_H

#include <stddef.h>
#include <stdint.h>

/*
 * Map a canonical DOSType value (e.g. 0x444F5301 for DOS/1) to a filesystem
 * basename in data/fs (e.g. "DOS.1"). Returns NULL if not mapped.
 */
const char *amiga_fsid_name_for_dostype(uint32_t canonical_dostype);

/*
 * Parse a filesystem basename ("TAG.N") into load_fs() dosID format:
 * dosid[0..2] = TAG chars, dosid[3] = numeric revision byte.
 * Returns 0 on success, -1 on failure.
 */
int amiga_fsid_basename_to_dosid(const char *basename, char dosid[4]);

/*
 * Convert raw on-wire (big-endian) DOSType into load_fs() dosID format and
 * optional printable "TAG/n" string.
 * Returns 0 on success, -1 if no valid mapping/format can be produced.
 */
int amiga_fsid_build_dosid(uint32_t raw_dostype, char dosid[4], char *dosid_str,
                           size_t dosid_str_len);

#endif
