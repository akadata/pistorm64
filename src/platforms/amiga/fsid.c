// SPDX-License-Identifier: MIT

#include "platforms/amiga/fsid.h"

#include <ctype.h>
#include <endian.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t dostype;
    const char *basename;
} fs_map_entry_t;

static const fs_map_entry_t fs_map[] = {
    { 0x444F5300u, "DOS.0" }, /* DOS/0 OFS non-intl */
    { 0x444F5301u, "DOS.1" }, /* DOS/1 FFS non-intl */
    { 0x444F5302u, "DOS.2" }, /* DOS/2 OFS intl */
    { 0x444F5303u, "DOS.3" }, /* DOS/3 FFS intl */
    { 0x444F5304u, "DOS.4" }, /* DOS/4 OFS intl, dircache */
    { 0x444F5305u, "DOS.5" }, /* DOS/5 FFS intl, dircache */
    { 0x444F5306u, "DOS.6" }, /* DOS/6 OFS LNFS */
    { 0x444F5307u, "DOS.7" }, /* DOS/7 FFS LNFS */

    { 0x50465300u, "PFS.0" }, /* PFS/0 */
    { 0x50465301u, "PFS.1" }, /* PFS/1 */
    { 0x50465302u, "PFS.2" }, /* PFS/2 */
    { 0x50465303u, "PFS.3" }, /* PFS/3 */

    { 0x50445302u, "PDS.2" }, /* PDS/2 PFS2 SCSIdirect */
    { 0x50445303u, "PDS.3" }, /* PDS/3 PFS3 SCSIdirect */

    { 0x53465300u, "SFS.0" }, /* SFS/0 SmartFS v1 */
    { 0x53465302u, "SFS.2" }, /* SFS/2 SmartFS v2 */

    { 0x4D534400u, "MSD.0" }, /* MSD/0 MS-DOS disk */
    { 0x4D534800u, "MSH.0" }, /* MSH/0 PC-Task hardfile */

    { 0x554E4900u, "UNI.0" }, /* UNI/0 AMIX */
    { 0x554E4901u, "UNI.1" }, /* UNI/1 */

    { 0u, NULL }
};

const char *amiga_fsid_name_for_dostype(uint32_t canonical_dostype)
{
    const fs_map_entry_t *m = fs_map;
    while (m->basename != NULL) {
        if (m->dostype == canonical_dostype) {
            return m->basename;
        }
        m++;
    }
    return NULL;
}

int amiga_fsid_basename_to_dosid(const char *basename, char dosid[4])
{
    if (!basename || !dosid) {
        return -1;
    }
    if (strlen(basename) < 5 || basename[3] != '.') {
        return -1;
    }
    if (!isprint((unsigned char)basename[0]) ||
        !isprint((unsigned char)basename[1]) ||
        !isprint((unsigned char)basename[2])) {
        return -1;
    }

    dosid[0] = basename[0];
    dosid[1] = basename[1];
    dosid[2] = basename[2];

    if (basename[4] >= '0' && basename[4] <= '9') {
        dosid[3] = (char)(basename[4] - '0');
        return 0;
    }
    if (basename[4] >= 'A' && basename[4] <= 'F') {
        dosid[3] = (char)(10 + (basename[4] - 'A'));
        return 0;
    }
    if (basename[4] >= 'a' && basename[4] <= 'f') {
        dosid[3] = (char)(10 + (basename[4] - 'a'));
        return 0;
    }

    return -1;
}

int amiga_fsid_build_dosid(uint32_t raw_dostype, char dosid[4], char *dosid_str,
                           size_t dosid_str_len)
{
    uint32_t canonical;
    const char *basename;

    if (!dosid) {
        return -1;
    }

    canonical = be32toh(raw_dostype);
    basename = amiga_fsid_name_for_dostype(canonical);
    if (basename != NULL && amiga_fsid_basename_to_dosid(basename, dosid) == 0) {
        if (dosid_str && dosid_str_len > 0) {
            snprintf(dosid_str, dosid_str_len, "%c%c%c/%u",
                     dosid[0], dosid[1], dosid[2], (unsigned int)(uint8_t)dosid[3]);
        }
        return 0;
    }

    dosid[0] = (char)((canonical >> 24) & 0xFFu);
    dosid[1] = (char)((canonical >> 16) & 0xFFu);
    dosid[2] = (char)((canonical >> 8) & 0xFFu);
    dosid[3] = (char)(canonical & 0xFFu);
    if (!isprint((unsigned char)dosid[0]) ||
        !isprint((unsigned char)dosid[1]) ||
        !isprint((unsigned char)dosid[2])) {
        return -1;
    }

    if (dosid_str && dosid_str_len > 0) {
        snprintf(dosid_str, dosid_str_len, "%c%c%c/%u",
                 dosid[0], dosid[1], dosid[2], (unsigned int)(uint8_t)dosid[3]);
    }
    return 0;
}
