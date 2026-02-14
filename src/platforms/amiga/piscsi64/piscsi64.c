// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <stddef.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <endian.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/fs.h>
#endif
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include "m68k.h"
#include "config_file/config_file.h"
#include "gpio/ps_protocol.h"
#include "log.h"
#include "piscsi64-enums.h"
#include "piscsi64.h"
#include "platforms/amiga/fsid.h"
#include "platforms/amiga/hunk-reloc.h"


extern int load_fs(struct piscsi64_fs *fs, char *dosID);

#define BE(val) be32toh(val)
#define BE16(val) be16toh(val)

extern struct emulator_config *cfg;

// Debug output is controlled at runtime via --log-level debug.
#define PISCSI64_DEBUG

#ifdef PISCSI64_DEBUG
#define DEBUG LOG_DEBUG
#define DEBUG_TRIVIAL LOG_DEBUG

//extern void stop_cpu_emulation(uint8_t disasm_cur);
#define stop_cpu_emulation(...)

static const char *op_type_names[4] = {
    "BYTE",
    "WORD",
    "LONGWORD",
    "MEM",
};

extern unsigned int cpu_type;
static __thread char piscsi64_disasm_buf[256];

static void piscsi64_dump_cpu_state(const char *tag) {
    if (log_get_level() < LOG_LEVEL_DEBUG) {
        return;
    }
    unsigned int pc = m68k_get_reg(NULL, M68K_REG_PC);
    unsigned int ppc = m68k_get_reg(NULL, M68K_REG_PPC);
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    unsigned int a7 = m68k_get_reg(NULL, M68K_REG_A7);
    int32_t map_idx = get_mapped_item_by_address(cfg, pc);
    if (map_idx >= 0) {
        LOG_DEBUG("[PISCSI64-CPU] PC map[%d] type=%u range=$%.8lX-$%.8lX id=%s\n",
                  map_idx,
                  (unsigned int)cfg->map_type[map_idx],
                  cfg->map_offset[map_idx],
                  cfg->map_high[map_idx] - 1,
                  cfg->map_id[map_idx] ? cfg->map_id[map_idx] : "None");
    } else {
        LOG_DEBUG("[PISCSI64-CPU] PC map: unmapped\n");
    }
    LOG_DEBUG("[PISCSI64-CPU] %s PC=$%.8X PPC=$%.8X SR=$%.4X\n", tag ? tag : "state", pc, ppc, sr);
    m68k_disassemble(piscsi64_disasm_buf, pc, cpu_type);
    LOG_DEBUG("[PISCSI64-CPU] %s\n", piscsi64_disasm_buf);
    LOG_DEBUG("[PISCSI64-CPU] REGA: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n",
              m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
              m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3),
              m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5),
              m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7));
    LOG_DEBUG("[PISCSI64-CPU] REGD: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n",
              m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
              m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
              m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5),
              m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));

    uint8_t pc_bytes[18];
    for (int i = -8; i < 10; i++) {
        pc_bytes[i + 8] = (uint8_t)m68k_read_memory_8(pc + (uint32_t)i);
    }
    char pc_line[128];
    int pc_off = 0;
    pc_off += snprintf(pc_line + pc_off, sizeof(pc_line) - (size_t)pc_off, "[PISCSI64-CPU] PC bytes:");
    for (int i = 0; i < 18; i++) {
        pc_off += snprintf(pc_line + pc_off, sizeof(pc_line) - (size_t)pc_off, " %.2X", pc_bytes[i]);
    }
    LOG_DEBUG("%s\n", pc_line);

    LOG_DEBUG("[PISCSI64-CPU] A7=$%.8X stack longs:", a7);
    for (int i = 0; i < 8; i++) {
        uint32_t val = (uint32_t)m68k_read_memory_32(a7 + (uint32_t)(i * 4));
        LOG_DEBUG(" %.8X", val);
    }
    LOG_DEBUG("\n");

    uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0);
    uint32_t a1 = m68k_get_reg(NULL, M68K_REG_A1);
    uint32_t a2 = m68k_get_reg(NULL, M68K_REG_A2);
    int32_t a0_map = get_mapped_item_by_address(cfg, a0);
    int32_t a1_map = get_mapped_item_by_address(cfg, a1);
    int32_t a2_map = get_mapped_item_by_address(cfg, a2);
    LOG_DEBUG("[PISCSI64-CPU] A0 map: %d A1 map: %d A2 map: %d\n", a0_map, a1_map, a2_map);

    if (pc_bytes[8] == 0x4C && pc_bytes[9] == 0xDF && pc_bytes[10] == 0x7F && pc_bytes[11] == 0xFF &&
        pc_bytes[12] == 0x4E && pc_bytes[13] == 0x75) {
        uint32_t a7_after = a7 + (15u * 4u);
        uint32_t retaddr = (uint32_t)m68k_read_memory_32(a7_after);
        LOG_DEBUG("[PISCSI64-CPU] movem.l (A7)+,D0-D7/A0-A6 -> A7=$%.8X RTS_ret=$%.8X\n", a7_after, retaddr);
        int32_t ret_map = get_mapped_item_by_address(cfg, retaddr);
        if (ret_map >= 0) {
            LOG_DEBUG("[PISCSI64-CPU] RTS target map[%d] type=%u range=$%.8lX-$%.8lX id=%s\n",
                      ret_map,
                      (unsigned int)cfg->map_type[ret_map],
                      cfg->map_offset[ret_map],
                      cfg->map_high[ret_map] - 1,
                      cfg->map_id[ret_map] ? cfg->map_id[ret_map] : "None");
        } else {
            LOG_DEBUG("[PISCSI64-CPU] RTS target map: unmapped\n");
        }
    }
}
#else
#define DEBUG(...)
#define DEBUG_TRIVIAL(...)
#define stop_cpu_emulation(...)
static inline void piscsi64_dump_cpu_state(const char *tag) {
    (void)tag;
}
#endif

#ifdef FAKESTORM
#define lseek64 lseek
#endif

extern struct emulator_config *cfg;

struct piscsi64_dev piscsi64_devs[PISCSI64_NUM_UNITS];
struct piscsi64_fs piscsi64_filesystems[NUM_FILESYSTEMS];

uint8_t piscsi64_num_fs = 0;

#define FS_ALLOC_MAX_BYTES (512 * 1024)

static void piscsi64_log_unit_summary(const char *reason);
static void piscsi64_clear_media_runtime(struct piscsi64_dev *d);

#define PISCSI64_REMOTE_DEFAULT_PORT 4964
#define PISCSI64_REMOTE_MAGIC_HELLO 0x50533634u /* "PS64" */
#define PISCSI64_REMOTE_MAGIC_IOREQ 0x50533640u /* "PS6@" */
#define PISCSI64_REMOTE_MAGIC_IORSP 0x50533641u /* "PS6A" */
#define PISCSI64_REMOTE_VERSION 1u

#define PISCSI64_REMOTE_FLAG_REQ_RW    (1u << 0)
#define PISCSI64_REMOTE_FLAG_HINT_CD   (1u << 1)

#define PISCSI64_REMOTE_OP_READ  1u
#define PISCSI64_REMOTE_OP_WRITE 2u
#define PISCSI64_REMOTE_OP_SYNC  3u
#define PISCSI64_REMOTE_OP_CLOSE 4u
#define PISCSI64_REMOTE_OP_PING  5u
#define PISCSI64_REMOTE_SLOW_IO_MS 100u
#define PISCSI64_REMOTE_SLOW_PING_MS 50u
#define PISCSI64_FALLBACK_IO_CHUNK 4096u

typedef struct __attribute__((packed)) piscsi64_remote_hello_req {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t flags_be;
    uint16_t token_len_be;
    uint16_t export_len_be;
    uint32_t reserved_be;
    uint8_t client_nonce[16];
} piscsi64_remote_hello_req_t;

typedef struct __attribute__((packed)) piscsi64_remote_hello_rsp {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t status_be;
    uint64_t size_bytes_be;
    uint32_t block_size_be;
    uint8_t media_kind;
    uint8_t read_only;
    uint16_t reserved_be;
    uint8_t server_nonce[16];
} piscsi64_remote_hello_rsp_t;

typedef struct __attribute__((packed)) piscsi64_remote_io_req {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t op_be;
    uint64_t offset_be;
    uint32_t length_be;
    uint32_t reserved_be;
} piscsi64_remote_io_req_t;

typedef struct __attribute__((packed)) piscsi64_remote_io_rsp {
    uint32_t magic_be;
    uint16_t version_be;
    uint16_t status_be;
    uint32_t length_be;
    int32_t err_be;
} piscsi64_remote_io_rsp_t;

static int piscsi64_unit_index(const struct piscsi64_dev *d)
{
    if (!d) {
        return -1;
    }
    ptrdiff_t idx = d - piscsi64_devs;
    if (idx < 0 || idx >= (ptrdiff_t)PISCSI64_NUM_UNITS) {
        return -1;
    }
    return (int)idx;
}

static const char *piscsi64_media_kind_name(uint8_t media_kind)
{
    switch (media_kind) {
        case PISCSI64_MEDIA_DISK:
            return "disk";
        case PISCSI64_MEDIA_CDROM:
            return "cdrom";
        default:
            return "unknown";
    }
}

static const char *piscsi64_remote_status_name(uint16_t status)
{
    switch (status) {
        case 0u:
            return "ok";
        case 1u:
            return "auth";
        case 2u:
            return "export";
        case 3u:
            return "open";
        case 4u:
            return "badreq";
        default:
            return "unknown";
    }
}

static int piscsi64_is_media_loss_errno(int errnum)
{
    switch (errnum) {
        case ENODEV:
        case ENXIO:
        case ENOMEDIUM:
        case EIO:
        case ESTALE:
        case EBADF:
        case ENOTCONN:
        case ECONNRESET:
        case ECONNABORTED:
        case EPIPE:
        case ETIMEDOUT:
        case EHOSTUNREACH:
        case ENETDOWN:
        case ENETUNREACH:
            return 1;
        default:
            return 0;
    }
}

static uint64_t piscsi64_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000ull) + (uint64_t)(ts.tv_nsec / 1000000ull);
}

typedef struct piscsi64_tls_psk_client_data {
    char identity[128];
    char token[128];
} piscsi64_tls_psk_client_data_t;

static unsigned int piscsi64_tls_psk_client_cb(SSL *ssl, const char *hint,
                                               char *identity, unsigned int max_identity_len,
                                               unsigned char *psk, unsigned int max_psk_len)
{
    (void)hint;
    const piscsi64_tls_psk_client_data_t *psk_data =
        (const piscsi64_tls_psk_client_data_t *)SSL_get_app_data(ssl);
    if (!psk_data || !psk_data->token[0] || !psk_data->identity[0]) {
        return 0;
    }
    size_t id_len = strlen(psk_data->identity);
    size_t key_len = strlen(psk_data->token);
    if (id_len == 0 || key_len == 0 || id_len + 1 > max_identity_len || key_len > max_psk_len) {
        return 0;
    }
    memcpy(identity, psk_data->identity, id_len + 1);
    memcpy(psk, psk_data->token, key_len);
    return (unsigned int)key_len;
}

static int piscsi64_tls_send_all(SSL *ssl, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        int chunk = (len > (size_t)INT_MAX) ? INT_MAX : (int)len;
        int n = SSL_write(ssl, p, chunk);
        if (n <= 0) {
            int e = SSL_get_error(ssl, n);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                continue;
            }
            errno = EIO;
            return -1;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int piscsi64_tls_recv_all(SSL *ssl, void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    while (len > 0) {
        int chunk = (len > (size_t)INT_MAX) ? INT_MAX : (int)len;
        int n = SSL_read(ssl, p, chunk);
        if (n <= 0) {
            int e = SSL_get_error(ssl, n);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                continue;
            }
            errno = ENOTCONN;
            return -1;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int piscsi64_parse_remote_endpoint(const char *spec,
                                          char *token, size_t token_len,
                                          char *host, size_t host_len,
                                          uint16_t *port_out,
                                          char *export_name, size_t export_len)
{
    if (!spec || !host || !port_out || !export_name) {
        return -1;
    }
    if (token && token_len > 0) {
        token[0] = '\0';
    }

    const char *raw = spec;
    if (strncasecmp(raw, "remote:", 7) == 0) {
        raw += 7;
    }
    if (!raw[0]) {
        return -1;
    }

    char tmp[PISCSI64_MAX_SPEC];
    size_t raw_len = strlen(raw);
    if (raw_len == 0 || raw_len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, raw, raw_len + 1);

    char *slash = strchr(tmp, '/');
    char *hostpart = tmp;
    if (slash) {
        *slash = '\0';
        size_t export_part_len = strlen(slash + 1);
        if (export_len == 0 || export_part_len >= export_len) {
            return -1;
        }
        memcpy(export_name, slash + 1, export_part_len + 1);
    } else {
        if (export_len < sizeof("default")) {
            return -1;
        }
        memcpy(export_name, "default", sizeof("default"));
    }

    char *at = strchr(hostpart, '@');
    if (at) {
        *at = '\0';
        if (token && token_len > 0) {
            size_t token_part_len = strlen(hostpart);
            if (token_part_len >= token_len) {
                return -1;
            }
            memcpy(token, hostpart, token_part_len + 1);
        }
        hostpart = at + 1;
    }

    if (!hostpart[0]) {
        return -1;
    }

    uint16_t port = PISCSI64_REMOTE_DEFAULT_PORT;
    char *colon = strrchr(hostpart, ':');
    if (colon) {
        *colon = '\0';
        long p = strtol(colon + 1, NULL, 10);
        if (p <= 0 || p > 65535) {
            return -1;
        }
        port = (uint16_t)p;
    }

    if (!hostpart[0]) {
        return -1;
    }

    size_t host_part_len = strlen(hostpart);
    if (host_len == 0 || host_part_len >= host_len) {
        return -1;
    }
    memcpy(host, hostpart, host_part_len + 1);
    *port_out = port;
    return 0;
}

static int piscsi64_remote_send_req(struct piscsi64_dev *d, uint16_t op,
                                    uint64_t offset, uint32_t length)
{
    SSL *ssl = (SSL *)d->remote_tls;
    if (!ssl) {
        errno = ENOTCONN;
        return -1;
    }
    piscsi64_remote_io_req_t req;
    req.magic_be = htobe32(PISCSI64_REMOTE_MAGIC_IOREQ);
    req.version_be = htobe16(PISCSI64_REMOTE_VERSION);
    req.op_be = htobe16(op);
    req.offset_be = htobe64(offset);
    req.length_be = htobe32(length);
    req.reserved_be = 0;
    return piscsi64_tls_send_all(ssl, &req, sizeof(req));
}

static int piscsi64_remote_recv_rsp(struct piscsi64_dev *d, piscsi64_remote_io_rsp_t *rsp)
{
    SSL *ssl = (SSL *)d->remote_tls;
    if (!ssl) {
        errno = ENOTCONN;
        return -1;
    }
    if (piscsi64_tls_recv_all(ssl, rsp, sizeof(*rsp)) != 0) {
        return -1;
    }
    if (be32toh(rsp->magic_be) != PISCSI64_REMOTE_MAGIC_IORSP ||
        be16toh(rsp->version_be) != PISCSI64_REMOTE_VERSION) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int piscsi64_remote_connect(int unit_index, const char *spec, int req_rw,
                                   enum piscsi64_media_kind hint_media,
                                   int *sock_out, uint64_t *size_out,
                                   uint32_t *block_size_out,
                                   uint8_t *media_kind_out,
                                   uint8_t *read_only_out,
                                   void **tls_ctx_out, void **tls_out)
{
    uint64_t t_start_ms = piscsi64_now_ms();
    char token[128];
    char host[128];
    char export_name[128];
    uint16_t port = PISCSI64_REMOTE_DEFAULT_PORT;
    if (piscsi64_parse_remote_endpoint(spec, token, sizeof(token), host, sizeof(host),
                                       &port, export_name, sizeof(export_name)) != 0) {
        LOG_ERROR("[PISCSI64-REMOTE] Unit %d endpoint parse failed.\n", unit_index);
        errno = EINVAL;
        return -1;
    }
    if (!token[0]) {
        LOG_ERROR("[PISCSI64-REMOTE] Unit %d token missing for %s:%u/%s.\n",
                  unit_index, host, (unsigned int)port, export_name);
        errno = EACCES;
        return -1;
    }
    LOG_INFO("[PISCSI64-REMOTE] Unit %d connecting to %s:%u/%s (req=%s hint=%s).\n",
             unit_index,
             host,
             (unsigned int)port,
             export_name,
             req_rw ? "rw" : "ro",
             piscsi64_media_kind_name((uint8_t)hint_media));

    char port_s[16];
    snprintf(port_s, sizeof(port_s), "%u", (unsigned int)port);
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int gai = getaddrinfo(host, port_s, &hints, &res);
    if (gai != 0) {
        LOG_ERROR("[PISCSI64-REMOTE] Unit %d DNS/connect lookup failed for %s:%u (%s).\n",
                  unit_index, host, (unsigned int)port, gai_strerror(gai));
        errno = ECONNREFUSED;
        return -1;
    }

    int sock = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0) {
            continue;
        }
        if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0) {
        LOG_ERROR("[PISCSI64-REMOTE] Unit %d TCP connect failed for %s:%u (errno=%d).\n",
                  unit_index, host, (unsigned int)port, errno);
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    SSL_CTX *tls_ctx = SSL_CTX_new(TLS_client_method());
    if (!tls_ctx) {
        close(sock);
        errno = EIO;
        return -1;
    }
    if (SSL_CTX_set_min_proto_version(tls_ctx, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(tls_ctx, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_cipher_list(tls_ctx, "PSK-AES256-GCM-SHA384:PSK-AES128-GCM-SHA256") != 1) {
        SSL_CTX_free(tls_ctx);
        close(sock);
        errno = EIO;
        return -1;
    }
    SSL_CTX_set_psk_client_callback(tls_ctx, piscsi64_tls_psk_client_cb);
    SSL *ssl = SSL_new(tls_ctx);
    if (!ssl) {
        SSL_CTX_free(tls_ctx);
        close(sock);
        errno = EIO;
        return -1;
    }
    piscsi64_tls_psk_client_data_t psk_data;
    memset(&psk_data, 0, sizeof(psk_data));
    snprintf(psk_data.identity, sizeof(psk_data.identity), "%s", export_name);
    snprintf(psk_data.token, sizeof(psk_data.token), "%s", token);
    SSL_set_app_data(ssl, &psk_data);
    SSL_set_fd(ssl, sock);
    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        errno = EACCES;
        return -1;
    }
    {
        int bits = 0;
        const char *tls_ver = SSL_get_version(ssl);
        const char *tls_cipher = SSL_get_cipher_name(ssl);
        bits = SSL_get_cipher_bits(ssl, NULL);
        LOG_INFO("[PISCSI64-REMOTE] Unit %d TLS established: version=%s cipher=%s bits=%d\n",
                 unit_index,
                 tls_ver ? tls_ver : "unknown",
                 tls_cipher ? tls_cipher : "unknown",
                 bits);
    }

    piscsi64_remote_hello_req_t req;
    memset(&req, 0, sizeof(req));
    if (RAND_bytes(req.client_nonce, sizeof(req.client_nonce)) != 1) {
        close(sock);
        LOG_ERROR("[PISCSI64-REMOTE] Unit %d nonce generation failed.\n", unit_index);
        errno = EIO;
        return -1;
    }
    uint16_t flags = 0;
    if (req_rw) {
        flags |= PISCSI64_REMOTE_FLAG_REQ_RW;
    }
    if (hint_media == PISCSI64_MEDIA_CDROM) {
        flags |= PISCSI64_REMOTE_FLAG_HINT_CD;
    }
    req.magic_be = htobe32(PISCSI64_REMOTE_MAGIC_HELLO);
    req.version_be = htobe16(PISCSI64_REMOTE_VERSION);
    req.flags_be = htobe16(flags);
    req.token_len_be = htobe16(0);
    req.export_len_be = htobe16((uint16_t)strlen(export_name));

    if (piscsi64_tls_send_all(ssl, &req, sizeof(req)) != 0 ||
        (export_name[0] && piscsi64_tls_send_all(ssl, export_name, strlen(export_name)) != 0)) {
        LOG_ERROR("[PISCSI64-REMOTE] Unit %d hello send failed (errno=%d).\n", unit_index, errno);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        return -1;
    }

    piscsi64_remote_hello_rsp_t rsp;
    if (piscsi64_tls_recv_all(ssl, &rsp, sizeof(rsp)) != 0) {
        LOG_ERROR("[PISCSI64-REMOTE] Unit %d hello recv failed (errno=%d).\n", unit_index, errno);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        return -1;
    }
    if (be32toh(rsp.magic_be) != PISCSI64_REMOTE_MAGIC_HELLO ||
        be16toh(rsp.version_be) != PISCSI64_REMOTE_VERSION) {
        LOG_ERROR("[PISCSI64-REMOTE] Unit %d hello protocol mismatch.\n", unit_index);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        errno = EPROTO;
        return -1;
    }
    uint16_t status = be16toh(rsp.status_be);
    if (status != 0) {
        LOG_ERROR("[PISCSI64-REMOTE] Unit %d hello rejected: status=%u (%s).\n",
                  unit_index, (unsigned int)status, piscsi64_remote_status_name(status));
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(tls_ctx);
        close(sock);
        errno = EACCES;
        return -1;
    }

    if (sock_out) {
        *sock_out = sock;
    } else {
        close(sock);
    }
    if (size_out) {
        *size_out = be64toh(rsp.size_bytes_be);
    }
    if (block_size_out) {
        *block_size_out = be32toh(rsp.block_size_be);
    }
    if (media_kind_out) {
        *media_kind_out = rsp.media_kind;
    }
    if (read_only_out) {
        *read_only_out = rsp.read_only ? 1u : 0u;
    }
    if (tls_ctx_out) {
        *tls_ctx_out = tls_ctx;
    } else {
        SSL_CTX_free(tls_ctx);
    }
    if (tls_out) {
        *tls_out = ssl;
    } else {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    uint64_t t_done_ms = piscsi64_now_ms();
    uint64_t elapsed_ms = (t_done_ms >= t_start_ms) ? (t_done_ms - t_start_ms) : 0;
    LOG_INFO("[PISCSI64-REMOTE] Unit %d connected in %llums: size=%llu block=%u kind=%s mode=%s.\n",
             unit_index,
             (unsigned long long)elapsed_ms,
             (unsigned long long)be64toh(rsp.size_bytes_be),
             (unsigned int)be32toh(rsp.block_size_be),
             piscsi64_media_kind_name(rsp.media_kind),
             (rsp.read_only ? "ro" : "rw"));
    return 0;
}

static int piscsi64_backend_file_close(struct piscsi64_dev *d)
{
    if (!d || d->fd == -1) {
        return 0;
    }
    int rc = close(d->fd);
    d->fd = -1;
    return rc;
}

static off64_t piscsi64_backend_file_seek(struct piscsi64_dev *d, off64_t offset, int whence)
{
    if (!d || d->fd == -1) {
        errno = EBADF;
        return (off64_t)-1;
    }
    return lseek64(d->fd, offset, whence);
}

static ssize_t piscsi64_backend_file_read(struct piscsi64_dev *d, void *buf, size_t count)
{
    if (!d || d->fd == -1) {
        errno = EBADF;
        return -1;
    }
    return read(d->fd, buf, count);
}

static ssize_t piscsi64_backend_file_write(struct piscsi64_dev *d, const void *buf, size_t count)
{
    if (!d || d->fd == -1) {
        errno = EBADF;
        return -1;
    }
    return write(d->fd, buf, count);
}

static ssize_t piscsi64_backend_file_pread(struct piscsi64_dev *d, void *buf, size_t count,
                                           off64_t offset)
{
    if (!d || d->fd == -1) {
        errno = EBADF;
        return -1;
    }
    return pread(d->fd, buf, count, offset);
}

static int piscsi64_backend_file_sync(struct piscsi64_dev *d)
{
    if (!d || d->fd == -1) {
        errno = EBADF;
        return -1;
    }
    return fsync(d->fd);
}

static const struct piscsi64_backend_ops piscsi64_backend_file_ops = {
    .name = "file",
    .close = piscsi64_backend_file_close,
    .seek = piscsi64_backend_file_seek,
    .read = piscsi64_backend_file_read,
    .write = piscsi64_backend_file_write,
    .pread = piscsi64_backend_file_pread,
    .sync = piscsi64_backend_file_sync,
};

static void piscsi64_mark_media_offline(struct piscsi64_dev *d, const char *op, int errnum)
{
    if (!d || d->fd == -1 || !piscsi64_is_media_loss_errno(errnum)) {
        return;
    }

    int unit = (int)(d - piscsi64_devs);
    LOG_WARN("[PISCSI64] Unit %d backend=%s went offline during %s (errno=%d).\n",
             unit,
             (d->backend_ops && d->backend_ops->name) ? d->backend_ops->name : "unknown",
             op ? op : "io",
             errnum);
    piscsi64_clear_media_runtime(d);
    piscsi64_log_unit_summary("offline");
}

static off64_t piscsi64_backend_remote_seek(struct piscsi64_dev *d, off64_t offset, int whence)
{
    uint64_t base = 0;
    if (!d || d->remote_sock < 0) {
        errno = EBADF;
        return (off64_t)-1;
    }

    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = d->remote_pos;
            break;
        case SEEK_END:
            base = d->fs;
            break;
        default:
            errno = EINVAL;
            return (off64_t)-1;
    }

    if (offset < 0 && (uint64_t)(-offset) > base) {
        errno = EINVAL;
        return (off64_t)-1;
    }
    d->remote_pos = (uint64_t)((int64_t)base + (int64_t)offset);
    return (off64_t)d->remote_pos;
}

static ssize_t piscsi64_backend_remote_pread(struct piscsi64_dev *d, void *buf, size_t count,
                                             off64_t offset)
{
    uint64_t t0_ms = piscsi64_now_ms();
    int unit = piscsi64_unit_index(d);
    if (!d || d->remote_sock < 0) {
        errno = EBADF;
        return -1;
    }
    if (count > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }

    if (piscsi64_remote_send_req(d, PISCSI64_REMOTE_OP_READ, (uint64_t)offset, (uint32_t)count) != 0) {
        DEBUG("[PISCSI64-REMOTE] Unit:%d READ send failed off=0x%llX len=%u errno=%d\n",
              unit, (unsigned long long)offset, (unsigned int)count, errno);
        return -1;
    }

    piscsi64_remote_io_rsp_t rsp;
    if (piscsi64_remote_recv_rsp(d, &rsp) != 0) {
        DEBUG("[PISCSI64-REMOTE] Unit:%d READ rsp failed off=0x%llX len=%u errno=%d\n",
              unit, (unsigned long long)offset, (unsigned int)count, errno);
        return -1;
    }
    if (be16toh(rsp.status_be) != 0) {
        int32_t remote_err = be32toh((uint32_t)rsp.err_be);
        errno = remote_err > 0 ? remote_err : EIO;
        DEBUG("[PISCSI64-REMOTE] Unit:%d READ remote error off=0x%llX len=%u err=%d\n",
              unit, (unsigned long long)offset, (unsigned int)count, errno);
        return -1;
    }

    uint32_t len = be32toh(rsp.length_be);
    if (len != (uint32_t)count) {
        errno = EPROTO;
        return -1;
    }
    if (len && piscsi64_tls_recv_all((SSL *)d->remote_tls, buf, len) != 0) {
        DEBUG("[PISCSI64-REMOTE] Unit:%d READ payload recv failed off=0x%llX len=%u errno=%d\n",
              unit, (unsigned long long)offset, (unsigned int)len, errno);
        return -1;
    }
    uint64_t t1_ms = piscsi64_now_ms();
    uint64_t dt_ms = (t1_ms >= t0_ms) ? (t1_ms - t0_ms) : 0;
    if (dt_ms >= PISCSI64_REMOTE_SLOW_IO_MS) {
        DEBUG("[PISCSI64-REMOTE] Unit:%d READ slow off=0x%llX len=%u took=%llums\n",
              unit, (unsigned long long)offset, (unsigned int)len, (unsigned long long)dt_ms);
    }
    return (ssize_t)len;
}

static ssize_t piscsi64_backend_remote_read(struct piscsi64_dev *d, void *buf, size_t count)
{
    ssize_t rc = piscsi64_backend_remote_pread(d, buf, count, (off64_t)d->remote_pos);
    if (rc > 0) {
        d->remote_pos += (uint64_t)rc;
    }
    return rc;
}

static ssize_t piscsi64_backend_remote_write(struct piscsi64_dev *d, const void *buf, size_t count)
{
    uint64_t t0_ms = piscsi64_now_ms();
    int unit = piscsi64_unit_index(d);

    if (!d || d->remote_sock < 0) {
        errno = EBADF;
        return -1;
    }
    if (count > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }

    if (piscsi64_remote_send_req(d, PISCSI64_REMOTE_OP_WRITE, d->remote_pos, (uint32_t)count) != 0) {
        DEBUG("[PISCSI64-REMOTE] Unit:%d WRITE send failed off=0x%llX len=%u errno=%d\n",
              unit, (unsigned long long)d->remote_pos, (unsigned int)count, errno);
        return -1;
    }
    if (count) {
        if (piscsi64_tls_send_all((SSL *)d->remote_tls, buf, count) != 0) {
            DEBUG("[PISCSI64-REMOTE] Unit:%d WRITE payload send failed off=0x%llX len=%u errno=%d\n",
                  unit, (unsigned long long)d->remote_pos, (unsigned int)count, errno);
            return -1;
        }
    }

    piscsi64_remote_io_rsp_t rsp;
    if (piscsi64_remote_recv_rsp(d, &rsp) != 0) {
        DEBUG("[PISCSI64-REMOTE] Unit:%d WRITE rsp failed off=0x%llX len=%u errno=%d\n",
              unit, (unsigned long long)d->remote_pos, (unsigned int)count, errno);
        return -1;
    }
    if (be16toh(rsp.status_be) != 0) {
        int32_t remote_err = be32toh((uint32_t)rsp.err_be);
        errno = remote_err > 0 ? remote_err : EIO;
        DEBUG("[PISCSI64-REMOTE] Unit:%d WRITE remote error off=0x%llX len=%u err=%d\n",
              unit, (unsigned long long)d->remote_pos, (unsigned int)count, errno);
        return -1;
    }

    uint32_t len = be32toh(rsp.length_be);
    if (len > count) {
        errno = EPROTO;
        return -1;
    }
    d->remote_pos += (uint64_t)len;
    uint64_t t1_ms = piscsi64_now_ms();
    uint64_t dt_ms = (t1_ms >= t0_ms) ? (t1_ms - t0_ms) : 0;
    if (dt_ms >= PISCSI64_REMOTE_SLOW_IO_MS) {
        DEBUG("[PISCSI64-REMOTE] Unit:%d WRITE slow off=0x%llX len=%u took=%llums\n",
              unit, (unsigned long long)(d->remote_pos - len), (unsigned int)len, (unsigned long long)dt_ms);
    }
    return (ssize_t)len;
}

static int piscsi64_backend_remote_sync(struct piscsi64_dev *d)
{
    uint64_t t0_ms = piscsi64_now_ms();
    int unit = piscsi64_unit_index(d);
    if (!d || d->remote_sock < 0) {
        errno = EBADF;
        return -1;
    }

    if (piscsi64_remote_send_req(d, PISCSI64_REMOTE_OP_SYNC, 0, 0) != 0) {
        DEBUG("[PISCSI64-REMOTE] Unit:%d SYNC send failed errno=%d\n", unit, errno);
        return -1;
    }

    piscsi64_remote_io_rsp_t rsp;
    if (piscsi64_remote_recv_rsp(d, &rsp) != 0) {
        DEBUG("[PISCSI64-REMOTE] Unit:%d SYNC rsp failed errno=%d\n", unit, errno);
        return -1;
    }
    if (be16toh(rsp.status_be) != 0) {
        int32_t remote_err = be32toh((uint32_t)rsp.err_be);
        errno = remote_err > 0 ? remote_err : EIO;
        DEBUG("[PISCSI64-REMOTE] Unit:%d SYNC remote error err=%d\n", unit, errno);
        return -1;
    }
    uint64_t t1_ms = piscsi64_now_ms();
    uint64_t dt_ms = (t1_ms >= t0_ms) ? (t1_ms - t0_ms) : 0;
    if (dt_ms >= PISCSI64_REMOTE_SLOW_IO_MS) {
        DEBUG("[PISCSI64-REMOTE] Unit:%d SYNC slow took=%llums\n",
              unit, (unsigned long long)dt_ms);
    }
    return 0;
}

static int piscsi64_backend_remote_close(struct piscsi64_dev *d)
{
    if (!d || d->remote_sock < 0) {
        return 0;
    }
    (void)piscsi64_remote_send_req(d, PISCSI64_REMOTE_OP_CLOSE, 0, 0);
    if (d->remote_tls) {
        SSL_shutdown((SSL *)d->remote_tls);
        SSL_free((SSL *)d->remote_tls);
        d->remote_tls = NULL;
    }
    if (d->remote_tls_ctx) {
        SSL_CTX_free((SSL_CTX *)d->remote_tls_ctx);
        d->remote_tls_ctx = NULL;
    }
    close(d->remote_sock);
    d->remote_sock = -1;
    d->fd = -1;
    return 0;
}

static const struct piscsi64_backend_ops piscsi64_backend_block_ops = {
    .name = "block",
    .close = piscsi64_backend_file_close,
    .seek = piscsi64_backend_file_seek,
    .read = piscsi64_backend_file_read,
    .write = piscsi64_backend_file_write,
    .pread = piscsi64_backend_file_pread,
    .sync = piscsi64_backend_file_sync,
};

static const struct piscsi64_backend_ops piscsi64_backend_remote_ops = {
    .name = "remote",
    .close = piscsi64_backend_remote_close,
    .seek = piscsi64_backend_remote_seek,
    .read = piscsi64_backend_remote_read,
    .write = piscsi64_backend_remote_write,
    .pread = piscsi64_backend_remote_pread,
    .sync = piscsi64_backend_remote_sync,
};

static off64_t piscsi64_dev_seek(struct piscsi64_dev *d, off64_t offset, int whence)
{
    if (d && d->backend_ops && d->backend_ops->seek) {
        off64_t rc = d->backend_ops->seek(d, offset, whence);
        if (rc < 0) {
            piscsi64_mark_media_offline(d, "seek", errno);
        }
        return rc;
    }
    if (!d || d->fd == -1) {
        errno = EBADF;
        return (off64_t)-1;
    }
    return lseek64(d->fd, offset, whence);
}

static ssize_t piscsi64_dev_read(struct piscsi64_dev *d, void *buf, size_t count)
{
    if (d && d->backend_ops && d->backend_ops->read) {
        ssize_t rc = d->backend_ops->read(d, buf, count);
        if (rc < 0) {
            piscsi64_mark_media_offline(d, "read", errno);
        }
        return rc;
    }
    if (!d || d->fd == -1) {
        errno = EBADF;
        return -1;
    }
    return read(d->fd, buf, count);
}

static ssize_t piscsi64_dev_write(struct piscsi64_dev *d, const void *buf, size_t count)
{
    if (d && d->backend_ops && d->backend_ops->write) {
        ssize_t rc = d->backend_ops->write(d, buf, count);
        if (rc < 0) {
            piscsi64_mark_media_offline(d, "write", errno);
        }
        return rc;
    }
    if (!d || d->fd == -1) {
        errno = EBADF;
        return -1;
    }
    return write(d->fd, buf, count);
}

static ssize_t piscsi64_dev_pread(struct piscsi64_dev *d, void *buf, size_t count, off64_t offset)
{
    if (d && d->backend_ops && d->backend_ops->pread) {
        ssize_t rc = d->backend_ops->pread(d, buf, count, offset);
        if (rc < 0) {
            piscsi64_mark_media_offline(d, "pread", errno);
        }
        return rc;
    }
    if (!d || d->fd == -1) {
        errno = EBADF;
        return -1;
    }
    return pread(d->fd, buf, count, offset);
}

static int piscsi64_dev_close(struct piscsi64_dev *d)
{
    if (!d) {
        return -1;
    }
    if (d->backend_ops && d->backend_ops->close) {
        return d->backend_ops->close(d);
    }
    if (d->fd != -1) {
        int rc = close(d->fd);
        d->fd = -1;
        return rc;
    }
    return 0;
}

static int piscsi64_probe_media_online(struct piscsi64_dev *d)
{
    if (!d || d->fd < 0) {
        return 0;
    }

#ifdef __linux__
    if (d->backend_type == PISCSI64_BACKEND_BLOCK) {
        uint64_t bytes = 0;
        if (ioctl(d->fd, BLKGETSIZE64, &bytes) == -1) {
            piscsi64_mark_media_offline(d, "probe", errno);
            return 0;
        }
        d->fs = bytes;
    }
#endif
    if (d->backend_type == PISCSI64_BACKEND_REMOTE) {
        const uint64_t now_ms = piscsi64_now_ms();
        const uint64_t ping_start_ms = now_ms;
        int unit = piscsi64_unit_index(d);
        if (d->remote_last_probe_ms != 0 &&
            now_ms > d->remote_last_probe_ms &&
            (now_ms - d->remote_last_probe_ms) < 500) {
            return (d->fd >= 0);
        }

        if (piscsi64_remote_send_req(d, PISCSI64_REMOTE_OP_PING, 0, 0) != 0) {
            piscsi64_mark_media_offline(d, "probe-ping-send", errno);
            return 0;
        }
        piscsi64_remote_io_rsp_t rsp;
        if (piscsi64_remote_recv_rsp(d, &rsp) != 0) {
            piscsi64_mark_media_offline(d, "probe-ping-recv", errno);
            return 0;
        }
        if (be16toh(rsp.status_be) != 0) {
            errno = EIO;
            piscsi64_mark_media_offline(d, "probe-ping-status", errno);
            return 0;
        }
        d->remote_last_probe_ms = now_ms;
        uint64_t ping_end_ms = piscsi64_now_ms();
        uint64_t ping_ms = (ping_end_ms >= ping_start_ms) ? (ping_end_ms - ping_start_ms) : 0;
        if (ping_ms >= PISCSI64_REMOTE_SLOW_PING_MS) {
            DEBUG("[PISCSI64-REMOTE] Unit:%d ping slow took=%llums\n",
                  unit, (unsigned long long)ping_ms);
        }
    }
    return (d->fd >= 0);
}

static int fs_handler_valid(const struct piscsi64_fs *fs, uint32_t handler_addr, uint8_t partition,
                            const char *dosID) {
    if (!fs || !fs->valid) {
        LOG_ERROR("[PISCSI64] Rejecting handler for %c%c%c/%d (partition %u): filesystem invalid\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition);
        return 0;
    }
    if (fs->h_info.alloc_size == 0 || fs->h_info.alloc_size > FS_ALLOC_MAX_BYTES) {
        LOG_ERROR("[PISCSI64] Rejecting handler for %c%c%c/%d (partition %u): invalid alloc_size=%u\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition, fs->h_info.alloc_size);
        return 0;
    }
    if ((handler_addr & 1u) != 0) {
        LOG_ERROR("[PISCSI64] Rejecting handler for %c%c%c/%d (partition %u): handler=0x%08X not aligned\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition, handler_addr);
        return 0;
    }
    if (!fs->h_info.hunk_offsets || fs->h_info.num_hunks == 0 ||
        fs->h_info.hunk_offsets[0] >= fs->h_info.alloc_size) {
        LOG_ERROR("[PISCSI64] Rejecting handler for %c%c%c/%d (partition %u): invalid hunk table\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition);
        return 0;
    }
    if (fs->h_info.base_offset == 0 || handler_addr < fs->h_info.base_offset ||
        (handler_addr - fs->h_info.base_offset) >= fs->h_info.alloc_size) {
        LOG_ERROR("[PISCSI64] Rejecting handler for %c%c%c/%d (partition %u): handler=0x%08X "
                  "outside buffer base=0x%08X size=0x%08X\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition, handler_addr,
                  fs->h_info.base_offset, fs->h_info.alloc_size);
        return 0;
    }
    if (fs->h_info.header_size >= fs->h_info.alloc_size || (fs->h_info.header_size & 3u) != 0) {
        LOG_ERROR("[PISCSI64] Rejecting handler for %c%c%c/%d (partition %u): header_size=0x%08X "
                  "invalid for alloc=0x%08X\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition, fs->h_info.header_size,
                  fs->h_info.alloc_size);
        return 0;
    }
    uint32_t seglist_addr = handler_addr + fs->h_info.header_size;
    if (seglist_addr < fs->h_info.base_offset ||
        (seglist_addr - fs->h_info.base_offset) >= fs->h_info.alloc_size) {
        LOG_ERROR("[PISCSI64] Rejecting handler for %c%c%c/%d (partition %u): seglist=0x%08X "
                  "outside buffer base=0x%08X size=0x%08X\n",
                  dosID[0], dosID[1], dosID[2], dosID[3], partition, seglist_addr,
                  fs->h_info.base_offset, fs->h_info.alloc_size);
        return 0;
    }
    return 1;
}

static int piscsi64_get_map_bounds(struct emulator_config *cfg_local, uint32_t addr, uint32_t len,
                                 uint8_t **map_out, uint32_t *avail_out) {
    int32_t map_idx = get_mapped_item_by_address(cfg_local, addr);
    if (map_idx < 0) {
        if (map_out) {
            *map_out = NULL;
        }
        if (avail_out) {
            *avail_out = 0;
        }
        return -1;
    }

    if (cfg_local->map_type[map_idx] == MAPTYPE_ROM) {
        if (map_out) {
            *map_out = NULL;
        }
        if (avail_out) {
            *avail_out = 0;
        }
        LOG_ERROR("[PISCSI64] Refusing DMA into ROM map %d at 0x%08X\n", map_idx, addr);
        return -2;
    }

    uint32_t high = (uint32_t)cfg_local->map_high[map_idx];
    if (addr >= high) {
        if (map_out) {
            *map_out = NULL;
        }
        if (avail_out) {
            *avail_out = 0;
        }
        return -1;
    }

    uint32_t avail = high - addr;
    if (map_out) {
        *map_out = cfg_local->map_data[map_idx] + (addr - cfg_local->map_offset[map_idx]);
    }
    if (avail_out) {
        *avail_out = avail;
    }
    if (len > avail) {
        LOG_ERROR("[PISCSI64] Refusing %u-byte DMA at 0x%08X: exceeds map end (avail=%u)\n",
                  len, addr, avail);
        return -2;
    }

    return 0;
}

static int piscsi64_get_dma_window(struct emulator_config *cfg_local, uint8_t **buf_out,
                                 uint32_t *size_out, int32_t *map_idx_out) {
    int32_t idx = get_named_mapped_item(cfg_local, "piscsi64_dma");
    if (idx < 0 || !cfg_local->map_data[idx]) {
        return -1;
    }

    uint32_t size = (uint32_t)(cfg_local->map_high[idx] - cfg_local->map_offset[idx]);
    if (size == 0) {
        return -1;
    }

    if (buf_out) {
        *buf_out = cfg_local->map_data[idx];
    }
    if (size_out) {
        *size_out = size;
    }
    if (map_idx_out) {
        *map_idx_out = idx;
    }
    return 0;
}

uint8_t piscsi64_cur_drive = 0;
uint32_t piscsi64_u32[4];
uint32_t piscsi64_dbg[8];
uint32_t piscsi64_rom_size = 0;
uint8_t *piscsi64_rom_ptr;
static uint32_t last_debugme_idx = 0xFFFFFFFFu;
static int rom_read_logged = 0;
static int mmio_init_write_logged = 0;
static uint32_t mmio_init_write_count = 0;
static int rom_read_dot_trace = 0;
static int rom_read_progress_trace = 0;
static uint32_t rom_read_total_bytes = 0;
static uint32_t rom_read_bytes_since_log = 0;
static uint32_t rom_read_total_ops = 0;
static uint32_t rom_read_ops_since_line = 0;

uint32_t piscsi64_rom_partitions[128];
uint32_t piscsi64_rom_partition_prio[128];
uint32_t piscsi64_rom_partition_dostype[128];
uint32_t piscsi64_rom_cur_partition = 0, piscsi64_rom_cur_fs = 0;

extern unsigned char ac_piscsi64_rom[];

char piscsi64_partition_names[128][32];
unsigned int piscsi64_times_used[128];
unsigned int piscsi64_num_partition_names = 0;

struct hunk_info piscsi64_hinfo;
struct hunk_reloc piscsi64_hreloc[256];

static FILE *open_piscsi64_rom(char *chosen_path, size_t chosen_path_len) {
    if (chosen_path && chosen_path_len) {
        chosen_path[0] = '\0';
    }

    const char *candidates[4] = {0};
    char env_path[PATH_MAX];
    memset(env_path, 0x00, sizeof(env_path));

    const char *root = getenv("PISTORM_ROOT");
    if (root && root[0]) {
        snprintf(env_path, sizeof(env_path), "%s/src/platforms/amiga/piscsi64/piscsi64.rom", root);
        candidates[0] = env_path;
    }
    candidates[1] = "./src/platforms/amiga/piscsi64/piscsi64.rom";
    candidates[2] = "/opt/pistorm64/src/platforms/amiga/piscsi64/piscsi64.rom";
    candidates[3] = NULL;

    for (size_t i = 0; i < 3; i++) {
        const char *path = candidates[i];
        if (!path || !path[0]) {
            continue;
        }
        FILE *f = fopen(path, "rb");
        if (f) {
            if (chosen_path && chosen_path_len) {
                snprintf(chosen_path, chosen_path_len, "%s", path);
            }
            return f;
        }
    }

    return NULL;
}

static int str_ends_with_ci(const char *value, const char *suffix) {
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > value_len) {
        return 0;
    }
    return strcasecmp(value + value_len - suffix_len, suffix) == 0;
}

static uint8_t piscsi64_media_scsi_type(enum piscsi64_media_kind media_kind) {
    return (media_kind == PISCSI64_MEDIA_CDROM)
             ? (uint8_t)PISCSI64_SCSI_TYPE_CDROM
             : (uint8_t)PISCSI64_SCSI_TYPE_DIRECT_ACCESS;
}

static enum piscsi64_media_kind piscsi64_parse_media_spec(const char *spec, const char **path_out) {
    const char *path = spec;
    enum piscsi64_media_kind media_kind = PISCSI64_MEDIA_DISK;

    if (!spec || spec[0] == '\0') {
        if (path_out) {
            *path_out = spec;
        }
        return PISCSI64_MEDIA_NONE;
    }

    if (strncasecmp(spec, "disk:", 5) == 0) {
        path = spec + 5;
        media_kind = PISCSI64_MEDIA_DISK;
    } else if (strncasecmp(spec, "cdrom:", 6) == 0) {
        path = spec + 6;
        media_kind = PISCSI64_MEDIA_CDROM;
    } else {
        if (strncasecmp(spec, "file:", 5) == 0) {
            path = spec + 5;
        }
        media_kind = str_ends_with_ci(path, ".iso") ? PISCSI64_MEDIA_CDROM : PISCSI64_MEDIA_DISK;
    }

    if (path_out) {
        *path_out = path;
    }
    return media_kind;
}

static void piscsi64_parse_mode_opt(const char *opt, int *mode_opt)
{
    if (!opt || !mode_opt) {
        return;
    }
    if (strncasecmp(opt, "mode=", 5) != 0) {
        return;
    }

    const char *mode = opt + 5;
    if (strcasecmp(mode, "ro") == 0) {
        *mode_opt = 0;
    } else if (strcasecmp(mode, "rw") == 0) {
        *mode_opt = 1;
    } else {
        LOG_WARN("[PISCSI64] Unknown mode option '%s' (expected mode=ro|mode=rw)\n", opt);
    }
}

static int piscsi64_split_path_and_opts(const char *path_in, char *path_out, size_t path_out_sz,
                                        int *mode_opt)
{
    if (!path_in || !path_out || path_out_sz == 0) {
        return -1;
    }

    snprintf(path_out, path_out_sz, "%s", path_in);
    if (mode_opt) {
        *mode_opt = -1;
    }

    char *comma = strchr(path_out, ',');
    if (!comma) {
        return 0;
    }

    *comma = '\0';
    char *tok = comma + 1;
    while (tok && *tok) {
        char *next = strchr(tok, ',');
        if (next) {
            *next = '\0';
        }
        if (mode_opt) {
            piscsi64_parse_mode_opt(tok, mode_opt);
        }
        tok = next ? (next + 1) : NULL;
    }
    return 0;
}

static void piscsi64_clear_media_runtime(struct piscsi64_dev *d)
{
    if (!d) {
        return;
    }

    (void)piscsi64_dev_close(d);
    d->fd = -1;

    if (d->rdb) {
        free(d->rdb);
        d->rdb = NULL;
    }
    for (int i = 0; i < 16; i++) {
        if (d->pb[i]) {
            free(d->pb[i]);
            d->pb[i] = NULL;
        }
    }

    d->c = 0;
    d->h = 0;
    d->s = 0;
    d->fs = 0;
    d->lba = 0;
    d->num_partitions = 0;
    d->fshd_offs = 0;
    d->block_size = 0;
    d->remote_pos = 0;
    d->remote_last_probe_ms = 0;
    d->remote_tx_ctr = 0;
    d->remote_rx_ctr = 0;
    d->remote_crypto_enabled = 0;
    memset(d->remote_key, 0, sizeof(d->remote_key));
    memset(d->remote_iv, 0, sizeof(d->remote_iv));
    d->remote_tls = NULL;
    d->remote_tls_ctx = NULL;
    d->remote_block_size = 0;
    d->remote_media_kind = PISCSI64_MEDIA_NONE;
}

static void piscsi64_reset_dev(struct piscsi64_dev *d) {
    if (!d) {
        return;
    }

    piscsi64_clear_media_runtime(d);
    d->backend_type = PISCSI64_BACKEND_NONE;
    d->backend_ops = NULL;
    d->backend_spec[0] = '\0';
    d->configured_spec[0] = '\0';
    d->remote_sock = -1;
    d->remote_tls = NULL;
    d->remote_tls_ctx = NULL;
    d->media_kind = PISCSI64_MEDIA_NONE;
    d->read_only = 0;
}

void piscsi64_init(void) {
    const char *rom_dot_env = getenv("PISTORM_PISCSI64_ROM_DOTS");
    const char *rom_progress_env = getenv("PISTORM_PISCSI64_ROM_PROGRESS");
    rom_read_dot_trace = 0;
    rom_read_progress_trace = 0;
    if (rom_dot_env && (rom_dot_env[0] == '1' || rom_dot_env[0] == 'y' || rom_dot_env[0] == 'Y' ||
                        rom_dot_env[0] == 't' || rom_dot_env[0] == 'T')) {
        rom_read_dot_trace = 1;
    }
    if (rom_progress_env && (rom_progress_env[0] == '1' || rom_progress_env[0] == 'y' ||
                             rom_progress_env[0] == 'Y' || rom_progress_env[0] == 't' ||
                             rom_progress_env[0] == 'T')) {
        rom_read_progress_trace = 1;
    }

    rom_read_logged = 0;
    mmio_init_write_logged = 0;
    mmio_init_write_count = 0;
    rom_read_total_bytes = 0;
    rom_read_bytes_since_log = 0;
    rom_read_total_ops = 0;
    rom_read_ops_since_line = 0;

    for (int i = 0; i < PISCSI64_NUM_UNITS; i++) {
        piscsi64_devs[i].c = 0;
        piscsi64_devs[i].h = 0;
        piscsi64_devs[i].s = 0;
        piscsi64_devs[i].fs = 0;
        piscsi64_devs[i].fd = -1;
        piscsi64_devs[i].remote_sock = -1;
        piscsi64_devs[i].remote_pos = 0;
        piscsi64_devs[i].remote_last_probe_ms = 0;
        piscsi64_devs[i].remote_tx_ctr = 0;
        piscsi64_devs[i].remote_rx_ctr = 0;
        piscsi64_devs[i].remote_crypto_enabled = 0;
        memset(piscsi64_devs[i].remote_key, 0, sizeof(piscsi64_devs[i].remote_key));
        memset(piscsi64_devs[i].remote_iv, 0, sizeof(piscsi64_devs[i].remote_iv));
        piscsi64_devs[i].remote_tls = NULL;
        piscsi64_devs[i].remote_tls_ctx = NULL;
        piscsi64_devs[i].remote_block_size = 0;
        piscsi64_devs[i].remote_media_kind = PISCSI64_MEDIA_NONE;
        piscsi64_devs[i].backend_type = PISCSI64_BACKEND_NONE;
        piscsi64_devs[i].backend_ops = NULL;
        piscsi64_devs[i].backend_spec[0] = '\0';
        piscsi64_devs[i].configured_spec[0] = '\0';
        piscsi64_devs[i].lba = 0;
        piscsi64_devs[i].num_partitions = 0;
        piscsi64_devs[i].fshd_offs = 0;
        piscsi64_devs[i].block_size = 0;
        piscsi64_devs[i].media_kind = PISCSI64_MEDIA_NONE;
        piscsi64_devs[i].read_only = 0;
        piscsi64_devs[i].rdb = NULL;
        for (int j = 0; j < 16; j++) {
            piscsi64_devs[i].pb[j] = NULL;
        }
    }

    if (piscsi64_rom_ptr == NULL) {
        char rom_path[PATH_MAX];
        FILE *in = open_piscsi64_rom(rom_path, sizeof(rom_path));
        if (in == NULL) {
            LOG_ERROR("[PISCSI64] Could not open PiSCSI64 Boot ROM file for reading.\n");
            LOG_ERROR("[PISCSI64] Tried: $PISTORM_ROOT/src/platforms/amiga/piscsi64/piscsi64.rom, "
                      "./src/platforms/amiga/piscsi64/piscsi64.rom, "
                      "/opt/pistorm64/src/platforms/amiga/piscsi64/piscsi64.rom\n");
            // Zero out the boot ROM offset from the autoconfig ROM.
            ac_piscsi64_rom[20] = 0;
            ac_piscsi64_rom[21] = 0;
            ac_piscsi64_rom[22] = 0;
            ac_piscsi64_rom[23] = 0;
            return;
        }
        LOG_INFO("[PISCSI64] Loading Boot ROM from %s\n", rom_path[0] ? rom_path : "(unknown path)");
        fseek(in, 0, SEEK_END);
        piscsi64_rom_size = (uint32_t)ftell(in);
        fseek(in, 0, SEEK_SET);
        piscsi64_rom_ptr = malloc(piscsi64_rom_size);
        fread(piscsi64_rom_ptr, piscsi64_rom_size, 1, in);

        fseek(in, PISCSI64_DRIVER_OFFSET, SEEK_SET);
        process_hunks(in, &piscsi64_hinfo, piscsi64_hreloc, PISCSI64_DRIVER_OFFSET);
        uint32_t driver_size = 0x4000 - PISCSI64_DRIVER_OFFSET;
        piscsi64_hinfo.byte_size = driver_size;
        piscsi64_hinfo.alloc_size = driver_size + piscsi64_hinfo.bss_size;

        fclose(in);

        printf("[PISCSI64] Loaded Boot ROM.\n");
    } else {
        printf("[PISCSI64] Boot ROM already loaded.\n");
    }
    if (rom_read_dot_trace) {
        LOG_INFO("[PISCSI64-ROM] dot trace enabled (one '.' per ROM read, summary every 64 reads).\n");
    }
    fflush(stdout);
}

void piscsi64_shutdown(void) {
    if (rom_read_dot_trace && rom_read_ops_since_line != 0) {
        printf(" [PISCSI64-ROM] ops=%u bytes=%u (partial line)\n", rom_read_total_ops, rom_read_total_bytes);
    }
    LOG_INFO("[PISCSI64-ROM] total reads ops=%u bytes=%u\n", rom_read_total_ops, rom_read_total_bytes);
    printf("[PISCSI64] Shutting down PiSCSI.\n");
    for (int i = 0; i < PISCSI64_NUM_UNITS; i++) {
        piscsi64_reset_dev(&piscsi64_devs[i]);
    }

    for (int i = 0; i < NUM_FILESYSTEMS; i++) {
        if (piscsi64_filesystems[i].binary_data) {
            free(piscsi64_filesystems[i].binary_data);
            piscsi64_filesystems[i].binary_data = NULL;
        }
        if (piscsi64_filesystems[i].fhb) {
            free(piscsi64_filesystems[i].fhb);
            piscsi64_filesystems[i].fhb = NULL;
        }
        piscsi64_filesystems[i].h_info.current_hunk = 0;
        piscsi64_filesystems[i].h_info.reloc_hunks = 0;
        piscsi64_filesystems[i].FS_ID = 0;
        piscsi64_filesystems[i].handler = 0;
        piscsi64_filesystems[i].valid = 0;
    }
}

static void piscsi64_find_partitions(struct piscsi64_dev *d) {
    int cur_partition = 0;
    uint8_t tmp;

    for (int i = 0; i < 16; i++) {
        if (d->pb[i]) {
            free(d->pb[i]);
            d->pb[i] = NULL;
        }
    }

    if (!d->rdb || d->rdb->rdb_PartitionList == 0) {
        DEBUG("[PISCSI64] No partitions on disk.\n");
        return;
    }

    char *block = malloc(d->block_size);

    piscsi64_dev_seek(d, BE(d->rdb->rdb_PartitionList) * d->block_size, SEEK_SET);
next_partition:;
    piscsi64_dev_read(d, block, d->block_size);

    uint32_t first_temp;
    memcpy(&first_temp, &block[0], sizeof(first_temp));
    uint32_t first = be32toh(first_temp);
    if (first != PART_IDENTIFIER) {
        DEBUG("Entry at block %d is not a valid partition. Aborting.\n", BE(d->rdb->rdb_PartitionList));
        return;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
    /*
     * Cast alignment warning intentionally suppressed:
     * PiSCSI interprets Amiga-side structures that arrive as byte streams.
     * On the Pi side these are represented as C structs with 32-bit alignment.
     * The external contract guarantees these blocks are properly aligned as expected by the Amiga.
     */
    struct PartitionBlock *pb = (struct PartitionBlock *)((char *)block);
#pragma GCC diagnostic pop
    tmp = pb->pb_DriveName[0];
    pb->pb_DriveName[tmp + 1] = 0x00;
    printf("[PISCSI64] Partition %d: %s (%d)\n", cur_partition, pb->pb_DriveName + 1, pb->pb_DriveName[0]);
    DEBUG("Checksum: %.8X HostID: %d\n", BE(pb->pb_ChkSum), BE(pb->pb_HostID));
    DEBUG("Flags: %d (%.8X) Devflags: %d (%.8X)\n", BE(pb->pb_Flags), BE(pb->pb_Flags), BE(pb->pb_DevFlags), BE(pb->pb_DevFlags));
    d->pb[cur_partition] = pb;

    for (int i = 0; i < 128; i++) {
        if (strcmp((char *)pb->pb_DriveName + 1, piscsi64_partition_names[i]) == 0) {
            DEBUG("[PISCSI64] Duplicate partition name %s. Temporarily renaming to %s_%d.\n", pb->pb_DriveName + 1, pb->pb_DriveName + 1, piscsi64_times_used[i] + 1);
            piscsi64_times_used[i]++;
            sprintf((char *)pb->pb_DriveName + 1 + pb->pb_DriveName[0], "_%d", piscsi64_times_used[i]);
            pb->pb_DriveName[0] += 2;
            if (piscsi64_times_used[i] > 9)
                pb->pb_DriveName[0]++;
            goto partition_renamed;
        }
    }
    sprintf(piscsi64_partition_names[piscsi64_num_partition_names], "%s", pb->pb_DriveName + 1);
    piscsi64_num_partition_names++;

partition_renamed:
    if (d->pb[cur_partition]->pb_Next != 0xFFFFFFFF) {
        uint64_t next = be32toh(pb->pb_Next);
        block = malloc(d->block_size);
        piscsi64_dev_seek(d, (off64_t)(next * d->block_size), SEEK_SET);
        cur_partition++;
        DEBUG("[PISCSI64] Next partition at block %d.\n", be32toh(pb->pb_Next));
        goto next_partition;
    }
    DEBUG("[PISCSI64] No more partitions on disk.\n");
    d->num_partitions = (uint8_t)(cur_partition + 1);
    d->fshd_offs = (uint32_t)piscsi64_dev_seek(d, 0, SEEK_CUR);

    return;
}

static int piscsi64_parse_rdb(struct piscsi64_dev *d) {
    int i = 0;
    uint8_t *block = malloc(PISCSI64_MAX_BLOCK_SIZE);

    piscsi64_dev_seek(d, 0, SEEK_SET);
    for (i = 0; i < RDB_BLOCK_LIMIT; i++) {
        piscsi64_dev_read(d, block, PISCSI64_MAX_BLOCK_SIZE);
        uint32_t first_temp;
        memcpy(&first_temp, &block[0], sizeof(first_temp));
        uint32_t first = be32toh(first_temp);
        if (first == RDB_IDENTIFIER)
            goto rdb_found;
    }
    goto no_rdb_found;
rdb_found:;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
    /*
     * Cast alignment warning intentionally suppressed:
     * PiSCSI interprets Amiga-side structures that arrive as byte streams.
     * On the Pi side these are represented as C structs with 32-bit alignment.
     * The external contract guarantees these blocks are properly aligned as expected by the Amiga.
     */
    struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)((char *)block);
#pragma GCC diagnostic pop
    DEBUG("[PISCSI64] RDB found at block %d.\n", i);
    d->c = be32toh(rdb->rdb_Cylinders);
    d->h = (uint16_t)be32toh(rdb->rdb_Heads);
    d->s = (uint16_t)be32toh(rdb->rdb_Sectors);
    d->num_partitions = 0;
    DEBUG("[PISCSI64] RDB - first partition at block %d.\n", be32toh(rdb->rdb_PartitionList));
    d->block_size = be32toh(rdb->rdb_BlockBytes);
    DEBUG("[PISCSI64] Block size: %d. (%d)\n", be32toh(rdb->rdb_BlockBytes), d->block_size);
    if (d->rdb)
        free(d->rdb);
    d->rdb = rdb;
    sprintf(d->rdb->rdb_DriveInitName, "pi-scsi64.device");
    return 0;

no_rdb_found:;
    if (block)
        free(block);

    return -1;
}

void piscsi64_refresh_drives(void) {
    piscsi64_num_fs = 0;

    for (int i = 0; i < NUM_FILESYSTEMS; i++) {
        if (piscsi64_filesystems[i].binary_data) {
            free(piscsi64_filesystems[i].binary_data);
            piscsi64_filesystems[i].binary_data = NULL;
        }
        if (piscsi64_filesystems[i].fhb) {
            free(piscsi64_filesystems[i].fhb);
            piscsi64_filesystems[i].fhb = NULL;
        }
        piscsi64_filesystems[i].h_info.current_hunk = 0;
        piscsi64_filesystems[i].h_info.reloc_hunks = 0;
        piscsi64_filesystems[i].FS_ID = 0;
        piscsi64_filesystems[i].handler = 0;
        piscsi64_filesystems[i].valid = 0;
    }

    piscsi64_rom_cur_fs = 0;

    for (int i = 0; i < 128; i++) {
        memset(piscsi64_partition_names[i], 0x00, 32);
        piscsi64_times_used[i] = 0;
    }
    piscsi64_num_partition_names = 0;

    for (int i = 0; i < PISCSI64_NUM_UNITS; i++) {
        if (piscsi64_devs[i].fd != -1 &&
            piscsi64_devs[i].media_kind == PISCSI64_MEDIA_DISK) {
            piscsi64_parse_rdb(&piscsi64_devs[i]);
            piscsi64_find_partitions(&piscsi64_devs[i]);
            if (piscsi64_devs[i].backend_type != PISCSI64_BACKEND_REMOTE) {
                piscsi64_find_filesystems(&piscsi64_devs[i]);
            }
        }
    }
}

void piscsi64_find_filesystems(struct piscsi64_dev *d) {
    if (d->backend_type == PISCSI64_BACKEND_REMOTE) {
        DEBUG("[PISCSI64] Skipping FSHD extraction for remote unit (not yet supported).\n");
        return;
    }
    if (!d->num_partitions)
        return;

    uint8_t fs_found = 0;

    uint8_t *fhb_block = malloc(d->block_size);

    piscsi64_dev_seek(d, d->fshd_offs, SEEK_SET);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
    /*
     * Cast alignment warning intentionally suppressed:
     * PiSCSI interprets Amiga-side structures that arrive as byte streams.
     * On the Pi side these are represented as C structs with 32-bit alignment.
     * The external contract guarantees these blocks are properly aligned as expected by the Amiga.
     */
    struct FileSysHeaderBlock *fhb = (struct FileSysHeaderBlock *)((char *)fhb_block);
#pragma GCC diagnostic pop
    piscsi64_dev_read(d, fhb_block, d->block_size);

    while (BE(fhb->fhb_ID) == FS_IDENTIFIER) {
        char *dosID = (char *)&fhb->fhb_DosType;
#ifdef PISCSI64_DEBUG
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
            if (piscsi64_filesystems[i].FS_ID == fhb->fhb_DosType) {
                DEBUG("[FSHD] File system %c%c%c/%d already loaded. Skipping.\n", dosID[0], dosID[1], dosID[2], dosID[3]);
                if (BE(fhb->fhb_Next) == 0xFFFFFFFF)
                    goto fs_done;

                goto skip_fs_load_lseg;
            }
        }

        if (load_lseg(d->fd, &piscsi64_filesystems[piscsi64_num_fs].binary_data, &piscsi64_filesystems[piscsi64_num_fs].h_info, piscsi64_filesystems[piscsi64_num_fs].relocs, d->block_size) != -1) {
            piscsi64_filesystems[piscsi64_num_fs].FS_ID = fhb->fhb_DosType;
            piscsi64_filesystems[piscsi64_num_fs].fhb = fhb;
            piscsi64_filesystems[piscsi64_num_fs].valid = 1;
            printf("[FSHD] Loaded and set up file system %d: %c%c%c/%d\n", piscsi64_num_fs + 1, dosID[0], dosID[1], dosID[2], dosID[3]);
            {
                char fs_save_filename[256];
                memset(fs_save_filename, 0x00, 256);
                sprintf(fs_save_filename, "./data/fs/%c%c%c.%d", dosID[0], dosID[1], dosID[2], dosID[3]);
                FILE *save_fs = fopen(fs_save_filename, "rb");
                if (save_fs == NULL) {
                    save_fs = fopen(fs_save_filename, "wb+");
                    if (save_fs != NULL) {
                        fwrite(piscsi64_filesystems[piscsi64_num_fs].binary_data, piscsi64_filesystems[piscsi64_num_fs].h_info.byte_size, 1, save_fs);
                        fclose(save_fs);
                        printf("[FSHD] File system %c%c%c/%d saved to fs storage.\n", dosID[0], dosID[1], dosID[2], dosID[3]);
                    } else {
                        printf("[FSHD] Failed to save file system to fs storage. (Permission issues?)\n");
                    }
                } else {
                    fclose(save_fs);
                }
            }
            piscsi64_num_fs++;
        } else {
            piscsi64_filesystems[piscsi64_num_fs].valid = 0;
        }

skip_fs_load_lseg:;
        fs_found++;
        piscsi64_dev_seek(d, BE(fhb->fhb_Next) * d->block_size, SEEK_SET);
        fhb_block = malloc(d->block_size);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
        /*
         * Cast alignment warning intentionally suppressed:
         * PiSCSI interprets Amiga-side structures that arrive as byte streams.
         * On the Pi side these are represented as C structs with 32-bit alignment.
         * The external contract guarantees these blocks are properly aligned as expected by the Amiga.
         */
        fhb = (struct FileSysHeaderBlock *)((char *)fhb_block);
#pragma GCC diagnostic pop
        piscsi64_dev_read(d, fhb_block, d->block_size);
    }

    if (!fs_found) {
        DEBUG("[!!!FSHD] No file systems found on hard drive!\n");
    }

fs_done:;
    if (fhb_block)
        free(fhb_block);
}

struct piscsi64_dev *piscsi64_get_dev(uint8_t index) {
    return &piscsi64_devs[index];
}

int piscsi64_find_free_unit(void) {
    for (int i = 1; i < PISCSI64_NUM_UNITS; i++) {
        if (piscsi64_devs[i].fd == -1 && piscsi64_devs[i].configured_spec[0] == '\0') {
            return i;
        }
    }
    return -1;
}

void piscsi64_map_drive(const char *spec, uint8_t index) {
    if (index >= PISCSI64_NUM_UNITS) {
        LOG_ERROR("[PISCSI64] Drive index %d out of range. Unable to map spec %s to drive.\n",
                  index, spec ? spec : "(null)");
        return;
    }

    if (!spec || spec[0] == '\0') {
        LOG_ERROR("[PISCSI64] Empty drive spec for unit %d.\n", index);
        return;
    }

    const char *path_spec = NULL;
    char path[PATH_MAX];
    int mode_opt = -1; /* -1 default, 0 ro, 1 rw */
    enum piscsi64_backend_type backend_type = PISCSI64_BACKEND_FILE;
    enum piscsi64_media_kind media_kind = piscsi64_parse_media_spec(spec, &path_spec);
    if (piscsi64_split_path_and_opts(path_spec, path, sizeof(path), &mode_opt) != 0) {
        LOG_ERROR("[PISCSI64] Invalid drive spec '%s' for unit %d.\n", spec, index);
        return;
    }
    if (!path[0]) {
        LOG_ERROR("[PISCSI64] Invalid drive spec '%s' for unit %d.\n", spec, index);
        return;
    }

    if (strncasecmp(path, "remote:", 7) == 0) {
        backend_type = PISCSI64_BACKEND_REMOTE;
    } else if (strncmp(path, "/dev/", 5) == 0) {
        backend_type = PISCSI64_BACKEND_BLOCK;
    }

    int open_flags;
    if (media_kind == PISCSI64_MEDIA_CDROM) {
        open_flags = O_RDONLY;
    } else if (mode_opt == 0) {
        open_flags = O_RDONLY;
    } else if (mode_opt == 1) {
        open_flags = O_RDWR;
    } else if (backend_type == PISCSI64_BACKEND_BLOCK ||
               backend_type == PISCSI64_BACKEND_REMOTE) {
        /* Safety default for real block nodes and remote exports. */
        open_flags = O_RDONLY;
    } else {
        open_flags = O_RDWR;
    }
    int read_only = (open_flags == O_RDONLY) ? 1 : 0;
    int32_t tmp_fd = -1;
    uint64_t file_size = 0;
    uint32_t remote_block_size = 0;
    uint8_t remote_media_kind = PISCSI64_MEDIA_NONE;
    uint8_t remote_read_only = 0;
    void *remote_tls_ctx = NULL;
    void *remote_tls = NULL;

    if (backend_type == PISCSI64_BACKEND_REMOTE) {
        int req_rw = (media_kind != PISCSI64_MEDIA_CDROM && open_flags == O_RDWR) ? 1 : 0;
        if (piscsi64_remote_connect(index, path, req_rw, media_kind, &tmp_fd, &file_size,
                                    &remote_block_size, &remote_media_kind, &remote_read_only,
                                    &remote_tls_ctx, &remote_tls) != 0) {
            LOG_ERROR("[PISCSI64] Failed to connect remote backend %s for drive %d (errno=%d).\n",
                      path, index, errno);
            return;
        }
        if (remote_media_kind == PISCSI64_MEDIA_CDROM) {
            media_kind = PISCSI64_MEDIA_CDROM;
        }
        if (remote_read_only) {
            read_only = 1;
        }
    } else {
        tmp_fd = open(path, open_flags);
        if (tmp_fd == -1) {
            int first_errno = errno;
            if (backend_type == PISCSI64_BACKEND_FILE &&
                media_kind == PISCSI64_MEDIA_DISK &&
                (first_errno == EACCES || first_errno == EPERM || first_errno == EROFS)) {
                tmp_fd = open(path, O_RDONLY);
                if (tmp_fd != -1) {
                    read_only = 1;
                }
            }
            if (tmp_fd == -1) {
                LOG_ERROR("[PISCSI64] Failed to open %s, could not map drive %d (errno=%d).\n",
                          path, index, first_errno);
                return;
            }
        }

        off64_t file_size_off = lseek64(tmp_fd, 0, SEEK_END);
        if (file_size_off < 0) {
            printf("[PISCSI64] Failed to determine size for %s (unit %d).\n", path, index);
            close(tmp_fd);
            return;
        }
        file_size = (uint64_t)file_size_off;
        lseek64(tmp_fd, 0, SEEK_SET);
    }

    struct piscsi64_dev *d = &piscsi64_devs[index];
    piscsi64_reset_dev(d);

    d->fs = file_size;
    d->fd = tmp_fd;
    d->remote_sock = (backend_type == PISCSI64_BACKEND_REMOTE) ? tmp_fd : -1;
    d->remote_pos = 0;
    d->remote_last_probe_ms = 0;
    d->remote_tx_ctr = 1;
    d->remote_rx_ctr = 1;
    d->remote_crypto_enabled = 0;
    d->remote_tls_ctx = (backend_type == PISCSI64_BACKEND_REMOTE) ? remote_tls_ctx : NULL;
    d->remote_tls = (backend_type == PISCSI64_BACKEND_REMOTE) ? remote_tls : NULL;
    if (backend_type == PISCSI64_BACKEND_REMOTE) {
        memset(d->remote_key, 0, sizeof(d->remote_key));
        memset(d->remote_iv, 0, sizeof(d->remote_iv));
    } else {
        memset(d->remote_key, 0, sizeof(d->remote_key));
        memset(d->remote_iv, 0, sizeof(d->remote_iv));
    }
    d->remote_block_size = remote_block_size;
    d->remote_media_kind = remote_media_kind;
    d->backend_type = backend_type;
    if (backend_type == PISCSI64_BACKEND_BLOCK) {
        d->backend_ops = &piscsi64_backend_block_ops;
    } else if (backend_type == PISCSI64_BACKEND_REMOTE) {
        d->backend_ops = &piscsi64_backend_remote_ops;
    } else {
        d->backend_ops = &piscsi64_backend_file_ops;
    }
    snprintf(d->backend_spec, sizeof(d->backend_spec), "%s", path);
    snprintf(d->configured_spec, sizeof(d->configured_spec), "%s", spec);
    d->media_kind = media_kind;
    d->read_only = (uint8_t)read_only;
    LOG_INFO("[PISCSI64] Map %d: [%s] (%s%s,%s) - %lu bytes.\n",
             index, path,
             media_kind == PISCSI64_MEDIA_CDROM ? "cdrom" : "disk",
             d->read_only ? ",ro" : ",rw",
             (d->backend_ops && d->backend_ops->name) ? d->backend_ops->name : "unknown",
             (unsigned long)file_size);
    if ((backend_type == PISCSI64_BACKEND_BLOCK || backend_type == PISCSI64_BACKEND_REMOTE) &&
        !d->read_only) {
        LOG_WARN("[PISCSI64] Unit %d opened %s backend in RW mode: %s\n",
                 index,
                 (backend_type == PISCSI64_BACKEND_REMOTE) ? "remote" : "block",
                 path);
    }

    if (media_kind == PISCSI64_MEDIA_CDROM) {
        uint32_t cd_block = (backend_type == PISCSI64_BACKEND_REMOTE && remote_block_size) ? remote_block_size : 2048u;
        uint64_t blocks = (file_size / cd_block);
        d->block_size = cd_block;
        d->h = 1;
        d->s = 1;
        d->c = (uint32_t)((blocks == 0) ? 1 : ((blocks > UINT32_MAX) ? UINT32_MAX : blocks));
        d->num_partitions = 0;
        d->fshd_offs = 0;
        LOG_INFO("[PISCSI64] Unit %d configured as CD-ROM (%llu blocks @ %u bytes).\n",
                 index, (unsigned long long)blocks, cd_block);
        piscsi64_log_unit_summary("map");
        return;
    }

    uint8_t hdfID[4] = {0};
    ssize_t id_read = piscsi64_dev_pread(d, hdfID, sizeof(hdfID), 0);
    if (id_read == (ssize_t)sizeof(hdfID) &&
        (memcmp(hdfID, "DOS", 3) == 0 ||
         memcmp(hdfID, "PFS", 3) == 0 ||
         memcmp(hdfID, "PDS", 3) == 0 ||
         memcmp(hdfID, "SFS", 3) == 0 ||
         memcmp(hdfID, "MSH", 3) == 0 ||
         memcmp(hdfID, "MSD", 3) == 0 ||
         memcmp(hdfID, "UNI", 3) == 0)) {
        printf("[!!!PISCSI64] The disk image %s is a UAE Single Partition Hardfile!\n", path);
        printf("[!!!PISCSI64] Detected DOSType signature: %c%c%c\\x%02X (0x%02X%02X%02X%02X)\n",
               hdfID[0], hdfID[1], hdfID[2], hdfID[3], hdfID[0], hdfID[1], hdfID[2], hdfID[3]);
        printf("[!!!PISCSI64] WARNING: PiSCSI does NOT support UAE Single Partition Hardfiles!\n");
        printf("[!!!PISCSI64] PLEASE check the PiSCSI readme file in the GitHub repo for more information.\n");
        printf("[!!!PISCSI64] If this is merely an empty or placeholder file you've created to partition and format on the Amiga, please disregard this warning message.\n");
    }

    if (piscsi64_parse_rdb(d) == -1) {
        DEBUG("[PISCSI64] No RDB found on disk, making up some CHS values.\n");
        uint32_t fallback_block = (backend_type == PISCSI64_BACKEND_REMOTE && remote_block_size)
                                    ? remote_block_size
                                    : 512u;
        d->h = 16;
        d->s = 63;
        d->c = (uint32_t)((file_size / fallback_block) / (d->s * d->h));
        d->block_size = fallback_block;
    }
    printf("[PISCSI64] CHS: %d %d %d\n", d->c, d->h, d->s);

    printf ("Finding partitions.\n");
    piscsi64_find_partitions(d);
    printf ("Finding file systems.\n");
    if (backend_type != PISCSI64_BACKEND_REMOTE) {
        piscsi64_find_filesystems(d);
    } else {
        printf("[PISCSI64] Skipping FSHD extraction for remote unit (not yet supported).\n");
    }
    printf ("Done.\n");

    // Perform self-test to validate HDF integrity
    printf("[PISCSI64-SELFTEST] Running HDF integrity validation for drive %d...\n", index);
    if (!piscsi64_validate_hdf(d, path)) {
        printf("[PISCSI64-SELFTEST-ERROR] HDF validation failed for drive %d (%s)\n", index, path);
    } else {
        printf("[PISCSI64-SELFTEST-SUCCESS] HDF validation passed for drive %d (%s)\n", index, path);
    }
    piscsi64_log_unit_summary("map");
}

// HDF integrity validation function
int piscsi64_validate_hdf(struct piscsi64_dev *d, const char *filename) {
    if (!d || d->fd == -1) {
        printf("[PISCSI64-SELFTEST] ERROR: Invalid device or file descriptor\n");
        return 0;
    }

    // Test 1: Read RDB block 0 (first 512 bytes)
    uint8_t rdb_block[512];
    if (piscsi64_dev_seek(d, 0, SEEK_SET) == (off64_t)-1) {
        printf("[PISCSI64-SELFTEST] ERROR: Cannot seek to RDB block 0 in %s\n", filename);
        return 0;
    }

    ssize_t bytes_read = piscsi64_dev_read(d, rdb_block, 512);
    if (bytes_read < 512) {
        printf("[PISCSI64-SELFTEST] ERROR: Cannot read full RDB block 0 from %s (got %zd bytes)\n", filename, bytes_read);
        return 0;
    }

    // Verify RDB signature (should start with "RDSK")
    if (rdb_block[0] == 'R' && rdb_block[1] == 'D' && rdb_block[2] == 'S' && rdb_block[3] == 'K') {
        printf("[PISCSI64-SELFTEST] INFO: Valid RDB signature found in %s\n", filename);
    } else {
        // Not all HDFs have RDB, some are partitioned drives, so this isn't always an error
        printf("[PISCSI64-SELFTEST] INFO: No RDB signature found in %s (may be partitioned drive)\n", filename);
    }

    // Test 2: For DH0 (unit 0), try to read first partition block (usually at block 2 or offset 1024)
    if (d - piscsi64_devs == 0) { // This is unit 0 (DH0)
        printf("[PISCSI64-SELFTEST] Testing DH0 partition accessibility...\n");

        // Look for first partition block (usually at offset 1024 for standard Amiga HDFs)
        uint8_t boot_block[512];
        if (piscsi64_dev_seek(d, 1024, SEEK_SET) == (off64_t)-1) {
            printf("[PISCSI64-SELFTEST] ERROR: Cannot seek to DH0 boot block in %s\n", filename);
            return 0;
        }

        bytes_read = piscsi64_dev_read(d, boot_block, 512);
        if (bytes_read < 512) {
            printf("[PISCSI64-SELFTEST] ERROR: Cannot read DH0 boot block from %s (got %zd bytes)\n", filename, bytes_read);
            return 0;
        }

        // Check for DOS boot block signature (starts with 0x444F5300 = "DOS\0")
        uint32_t dos_sig = ((uint32_t)boot_block[0] << 24) | ((uint32_t)boot_block[1] << 16) | ((uint32_t)boot_block[2] << 8) | (uint32_t)boot_block[3];
        if (dos_sig == 0x444F5300) {
            printf("[PISCSI64-SELFTEST] SUCCESS: Valid DOS boot block signature found in DH0\n");
        } else {
            printf("[PISCSI64-SELFTEST] INFO: No DOS boot block signature in DH0 (signature: 0x%08X)\n", dos_sig);
        }
    }

    // Test 3: Verify we can seek to end of file
    off64_t file_end = piscsi64_dev_seek(d, 0, SEEK_END);
    if (file_end == (off64_t)-1) {
        printf("[PISCSI64-SELFTEST] ERROR: Cannot seek to end of file %s\n", filename);
        return 0;
    }

    if ((uint64_t)file_end != d->fs) {
        printf("[PISCSI64-SELFTEST] WARNING: File size mismatch: reported=%llu, actual=%lld\n",
               (unsigned long long)d->fs, (long long)file_end);
    }

    // Test 4: Try reading a few random blocks to verify integrity
    for (int i = 0; i < 3; i++) {
        off64_t test_offset = (i + 1) * 512 * 100; // Every 100th block for testing
        if (test_offset >= (off64_t)d->fs) {
            continue; // Skip if beyond file size
        }

        uint8_t test_block[512];
        if (piscsi64_dev_seek(d, test_offset, SEEK_SET) == (off64_t)-1) {
            printf("[PISCSI64-SELFTEST] ERROR: Cannot seek to test block at offset %lld in %s\n",
                   (long long)test_offset, filename);
            return 0;
        }

        bytes_read = piscsi64_dev_read(d, test_block, 512);
        if (bytes_read < 512) {
            printf("[PISCSI64-SELFTEST] ERROR: Cannot read test block at offset %lld from %s (got %zd bytes)\n",
                   (long long)test_offset, filename, bytes_read);
            return 0;
        }
    }

    return 1; // All tests passed
}

void piscsi64_unmap_drive(uint8_t index) {
    if (index >= PISCSI64_NUM_UNITS) {
        return;
    }
    if (piscsi64_devs[index].fd != -1) {
        DEBUG("[PISCSI64] Unmapped drive %d.\n", index);
    }
    piscsi64_reset_dev(&piscsi64_devs[index]);
}

static int piscsi64_media_eject(uint8_t index)
{
    if (index >= PISCSI64_NUM_UNITS) {
        return -1;
    }

    struct piscsi64_dev *d = &piscsi64_devs[index];
    if (d->fd == -1) {
        return 0;
    }

    piscsi64_clear_media_runtime(d);
    LOG_INFO("[PISCSI64] Media ejected from unit %u.\n", (unsigned int)index);
    piscsi64_log_unit_summary("eject");
    return 0;
}

static int piscsi64_media_insert(uint8_t index)
{
    if (index >= PISCSI64_NUM_UNITS) {
        return -1;
    }

    struct piscsi64_dev *d = &piscsi64_devs[index];
    if (d->fd != -1) {
        return 0;
    }
    if (d->configured_spec[0] == '\0') {
        LOG_WARN("[PISCSI64] Unit %u has no configured media spec; cannot insert.\n",
                 (unsigned int)index);
        return -1;
    }

    char spec[PISCSI64_MAX_SPEC];
    snprintf(spec, sizeof(spec), "%s", d->configured_spec);
    piscsi64_map_drive(spec, index);
    if (piscsi64_devs[index].fd == -1) {
        LOG_WARN("[PISCSI64] Failed to insert media for unit %u using spec '%s'.\n",
                 (unsigned int)index, spec);
        return -1;
    }

    LOG_INFO("[PISCSI64] Media inserted in unit %u: %s\n", (unsigned int)index, spec);
    piscsi64_log_unit_summary("insert");
    return 0;
}

static void piscsi64_log_unit_summary(const char *reason)
{
    int mapped = 0;
    LOG_INFO("[PISCSI64] Unit summary (%s):\n", reason ? reason : "state");
    for (int i = 1; i < PISCSI64_NUM_UNITS; i++) {
        const struct piscsi64_dev *d = &piscsi64_devs[i];
        if (d->configured_spec[0] == '\0') {
            continue;
        }
        mapped = 1;
        LOG_INFO("[PISCSI64]   unit=%d media=%s mode=%s backend=%s attached=%s spec=%s\n",
                 i,
                 (d->media_kind == PISCSI64_MEDIA_CDROM) ? "cdrom" : "disk",
                 d->read_only ? "ro" : "rw",
                 (d->backend_ops && d->backend_ops->name) ? d->backend_ops->name : "none",
                 (d->fd >= 0) ? "yes" : "no",
                 d->configured_spec);
    }
    if (!mapped) {
        LOG_INFO("[PISCSI64]   (no units mapped)\n");
    }
}

static __attribute__((unused)) const char *io_cmd_name(int index) {
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
            return "[!!!PISCSI64] Unhandled IO command";
    }
}

#define GETSCSINAME(a) case a: return ""#a"";
#define SCSIUNHANDLED(a) return "[!!!PISCSI64] Unhandled SCSI command "#a"";

static __attribute__((unused)) const char *scsi_cmd_name(int index) {
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
            return "[!!!PISCSI64] Unhandled SCSI command";
    }
}

static __attribute__((unused)) void print_piscsi64_debug_message(int index) {
    int32_t r = 0;

    switch (index) {
        case DBG_INIT:
            DEBUG("[PISCSI64] Initializing devices.\n");
            break;
        case DBG_OPENDEV:
            if ((int)piscsi64_dbg[0] != 255) {
                DEBUG("[PISCSI64] Opening device %d (%d). Flags: %d (%.2X)\n", (int)piscsi64_dbg[0], (int)piscsi64_dbg[2], (int)piscsi64_dbg[1], (int)piscsi64_dbg[1]);
            }
            break;
        case DBG_CLEANUP:
            DEBUG("[PISCSI64] Cleaning up.\n");
            break;
        case DBG_CHS:
            DEBUG("[PISCSI64] C/H/S: %d / %d / %d\n", (int)piscsi64_dbg[0], (int)piscsi64_dbg[1], (int)piscsi64_dbg[2]);
            break;
        case DBG_BEGINIO:
            DEBUG("[PISCSI64] BeginIO: io_Command: %d (%s) - io_Flags = %d - quick: %d\n", (int)piscsi64_dbg[0], io_cmd_name((int)piscsi64_dbg[0]), (int)piscsi64_dbg[1], (int)piscsi64_dbg[2]);
            break;
        case DBG_ABORTIO:
            DEBUG("[PISCSI64] AbortIO!\n");
            break;
        case DBG_SCSICMD:
            DEBUG("[PISCSI64] SCSI Command %d (%s)\n", (int)piscsi64_dbg[1], scsi_cmd_name((int)piscsi64_dbg[1]));
            DEBUG("Len: %d - %.2X %.2X %.2X - Command Length: %d\n", (int)piscsi64_dbg[0], (int)piscsi64_dbg[1], (int)piscsi64_dbg[2], (int)piscsi64_dbg[3], (int)piscsi64_dbg[4]);
            break;
        case DBG_SCSI_UNKNOWN_MODESENSE:
            DEBUG("[!!!PISCSI64] SCSI: Unknown modesense %.4X\n", (int)piscsi64_dbg[0]);
            break;
        case DBG_SCSI_UNKNOWN_COMMAND:
            DEBUG("[!!!PISCSI64] SCSI: Unknown command %.4X\n", (int)piscsi64_dbg[0]);
            break;
        case DBG_SCSIERR:
            DEBUG("[!!!PISCSI64] SCSI: An error occured: %.4X\n", (int)piscsi64_dbg[0]);
            break;
        case DBG_IOCMD:
            DEBUG_TRIVIAL("[PISCSI64] IO Command %d (%s)\n", (int)piscsi64_dbg[0], io_cmd_name((int)piscsi64_dbg[0]));
            break;
        case DBG_IOCMD_UNHANDLED:
            DEBUG("[!!!PISCSI64] WARN: IO command %.4X (%s) is unhandled by driver.\n", piscsi64_dbg[0], io_cmd_name((int)piscsi64_dbg[0]));
            break;
        case DBG_SCSI_FORMATDEVICE:
            DEBUG("[PISCSI64] Get SCSI FormatDevice MODE SENSE.\n");
            break;
        case DBG_SCSI_RDG:
            DEBUG("[PISCSI64] Get SCSI RDG MODE SENSE.\n");
            break;
        case DBG_SCSICMD_RW10:
#ifdef PISCSI64_DEBUG
            r = get_mapped_item_by_address(cfg, piscsi64_dbg[0]);
            struct SCSICmd_RW10 *rwdat = NULL;
            uint8_t data[10];
            if (r != -1) {
                uint32_t addr = (uint32_t)(piscsi64_dbg[0] - cfg->map_offset[r]);
                rwdat = (struct SCSICmd_RW10 *)(&cfg->map_data[r][addr]);
            }
            else {
                DEBUG_TRIVIAL("[RW10] scsiData: %.8X\n", piscsi64_dbg[0]);
                for (int i = 0; i < 10; i++) {
                    data[i] = read8((uint32_t)piscsi64_dbg[0] + (uint32_t)i);
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
            DEBUG_TRIVIAL("[PISCSI64] SCSI ModeSense debug. Data: %.8X\n", piscsi64_dbg[0]);
            r = get_mapped_item_by_address(cfg, piscsi64_dbg[0]);
            if (r != -1) {
#ifdef PISCSI64_DEBUG
                uint32_t addr = (uint32_t)(piscsi64_dbg[0] - cfg->map_offset[r]);
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
                DEBUG("[!!!PISCSI64] ModeSense data not immediately available.\n");
            }
            break;
        default:
            DEBUG("[!!!PISCSI64] No debug message available for index %d.\n", index);
            break;
    }
}

#define DEBUGME_SIMPLE(i, s) case i: DEBUG(s); break;

static void piscsi64_debugme(uint32_t index) {
        if (index != last_debugme_idx) {
        LOG_INFO("[PISCSI64-DEBUGME] idx=%u\n", index);
        last_debugme_idx = index;
        if (index >= 30 && index <= 41) {
            #ifdef PISCSI64_DEBUG
            piscsi64_dump_cpu_state("DEBUGME step");
            #endif
        }
    }
    switch (index) {
        DEBUGME_SIMPLE(1, "[PISCSI64-DEBUGME] Arrived at DiagEntry.\n");
        DEBUGME_SIMPLE(2, "[PISCSI64-DEBUGME] Arrived at BootEntry, for some reason.\n");
        DEBUGME_SIMPLE(3, "[PISCSI64-DEBUGME] Init: Interrupt disable.\n");
        DEBUGME_SIMPLE(4, "[PISCSI64-DEBUGME] Init: Copy/reloc driver.\n");
        DEBUGME_SIMPLE(5, "[PISCSI64-DEBUGME] Init: InitResident.\n");
        DEBUGME_SIMPLE(7, "[PISCSI64-DEBUGME] Init: Begin partition loop.\n");
        DEBUGME_SIMPLE(8, "[PISCSI64-DEBUGME] Init: Partition loop done. Cleaning up and returning to Exec.\n");
        DEBUGME_SIMPLE(9, "[PISCSI64-DEBUGME] Init: Load file systems.\n");
        DEBUGME_SIMPLE(10, "[PISCSI64-DEBUGME] Init: AllocMem for resident.\n");
        DEBUGME_SIMPLE(11, "[PISCSI64-DEBUGME] Init: Checking if resident is loaded.\n");
        DEBUGME_SIMPLE(12, "[PISCSI64-DEBUGME] DiagEntry: begin patch table.\n");
        DEBUGME_SIMPLE(13, "[PISCSI64-DEBUGME] DiagEntry: patching ROM-relative pointers.\n");
        DEBUGME_SIMPLE(14, "[PISCSI64-DEBUGME] DiagEntry: patch complete, returning success.\n");
        DEBUGME_SIMPLE(15, "[PISCSI64-DEBUGME] Init: Found driver RomTag signature in copied image.\n");
        DEBUGME_SIMPLE(16, "[PISCSI64-DEBUGME] Init: Driver RomTag scan failed, using legacy +0x28 fallback.\n");
        DEBUGME_SIMPLE(22, "[PISCSI64-DEBUGME] Arrived at BootEntry.\n");
        case 30:
            DEBUG("[PISCSI64-DEBUGME] LoadFileSystems: Opening FileSystem.resource.\n");
            piscsi64_rom_cur_fs = 0;
            break;
        DEBUGME_SIMPLE(33, "[PISCSI64-DEBUGME] FileSystem.resource not available, creating.\n");
        case 31:
            DEBUG("[PISCSI64-DEBUGME] OpenResource result: %d\n", piscsi64_u32[0]);
            break;
        case 32:
            DEBUG("[PISCSI64-DEBUGME] DEBUGME 32 marker.\n");
#ifdef PISCSI64_DEBUG
            piscsi64_dump_cpu_state("DEBUGME 32");
#endif
            break;
        case 35:
            DEBUG("[PISCSI64-DEBUGME] stuff output\n");
            break;
        case 36:
            DEBUG("[PISCSI64-DEBUGME] Debug pointers: %.8X %.8X %.8X %.8X\n", piscsi64_u32[0], piscsi64_u32[1], piscsi64_u32[2], piscsi64_u32[3]);
            break;
        default:
            // Handle undefined indexes by printing the index number
            DEBUG("[PISCSI64-DEBUGME] idx=%u (no string)\n", index);
            break;
    }

    (void)index;
}

void handle_piscsi64_write(uint32_t addr, uint32_t val, uint8_t type) {
    uint8_t *map;
#ifndef PISCSI64_DEBUG
    if (type) {}
#endif

    struct piscsi64_dev *d = &piscsi64_devs[0];
    if (piscsi64_cur_drive < PISCSI64_NUM_UNITS) {
        d = &piscsi64_devs[piscsi64_cur_drive];
    }

    uint16_t cmd = (addr & 0xFFFF);

    if (!mmio_init_write_logged &&
        (cmd == PISCSI64_CMD_ADDR1 || cmd == PISCSI64_CMD_ADDR2 ||
         cmd == PISCSI64_CMD_ADDR3 || cmd == PISCSI64_CMD_ADDR4 ||
         cmd == PISCSI64_CMD_DEBUGME)) {
        LOG_INFO("[PISCSI64-MMIO] first init write type=%s addr=$%.8X cmd=$%.4X val=$%.8X\n",
                 op_type_names[type], addr, cmd, val);
        mmio_init_write_logged = 1;
    }
    if ((cmd == PISCSI64_CMD_ADDR1 || cmd == PISCSI64_CMD_ADDR2 ||
         cmd == PISCSI64_CMD_ADDR3 || cmd == PISCSI64_CMD_ADDR4 ||
         cmd == PISCSI64_CMD_DEBUGME) &&
        mmio_init_write_count < 32) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        LOG_DEBUG("[PISCSI64-MMIO] init[%u] type=%s pc=$%.8X addr=$%.8X cmd=$%.4X val=$%.8X\n",
                  mmio_init_write_count, op_type_names[type], pc, addr, cmd, val);
        mmio_init_write_count++;
    }

    switch (cmd) {
        case PISCSI64_CMD_READ64:
        case PISCSI64_CMD_READ:
        case PISCSI64_CMD_READBYTES:
            if (val >= PISCSI64_NUM_UNITS) {
                DEBUG("[!!!PISCSI64] BUG: Attempted read from invalid drive %u.\n", val);
                break;
            }
            d = &piscsi64_devs[val];
            if (d->fd == -1) {
                DEBUG("[!!!PISCSI64] BUG: Attempted read from unmapped drive %d.\n", val);
                break;
            }
            if (!piscsi64_probe_media_online(d)) {
                DEBUG("[PISCSI64] READ refused: unit %d is offline.\n", val);
                break;
            }

            if (cmd == PISCSI64_CMD_READBYTES) {
                uint32_t src = piscsi64_u32[0];
                uint32_t block = src / d->block_size;
                d->lba = block;
                DEBUG("[PISCSI64-IO] Unit:%d CMD:READBYTES io_Offset:0x%X io_Length:%d LBA:0x%X file_offset:0x%X to_addr:0x%.8X\n", val, src, piscsi64_u32[1], block, src, piscsi64_u32[2]);
                piscsi64_dev_seek(d, (off64_t)src, SEEK_SET);
            }
            else if (cmd == PISCSI64_CMD_READ) {
                uint32_t block = piscsi64_u32[0];
                uint64_t file_offset = (uint64_t)block * d->block_size;
                d->lba = block;
                DEBUG("[PISCSI64-IO] Unit:%d CMD:READ io_Offset:0x%X io_Length:%d LBA:0x%X file_offset:0x%llX to_addr:0x%.8X\n", val, block, piscsi64_u32[1], block, (unsigned long long)file_offset, piscsi64_u32[2]);
                piscsi64_dev_seek(d, (off64_t)file_offset, SEEK_SET);
            }
            else {
                uint64_t src = ((uint64_t)piscsi64_u32[3] << 32) | piscsi64_u32[0];
                uint32_t block = (uint32_t)(src / d->block_size);
                d->lba = block;
                DEBUG("[PISCSI64-IO] Unit:%d CMD:READ64 io_Offset:0x%llX io_Length:%d LBA:0x%X file_offset:0x%llX to_addr:0x%.8X\n", val, (unsigned long long)src, piscsi64_u32[1], block, (unsigned long long)src, piscsi64_u32[2]);
                piscsi64_dev_seek(d, (off64_t)src, SEEK_SET);
            }

            uint32_t avail = 0;
            map = NULL;
            int map_rc_read = piscsi64_get_map_bounds(cfg, piscsi64_u32[2], piscsi64_u32[1], &map, &avail);
            if (map_rc_read == 0) {
#ifdef PISCSI64_DEBUG
                int32_t debug_r = get_mapped_item_by_address(cfg, piscsi64_u32[2]);
                DEBUG_TRIVIAL("[PISCSI64-%d] \"DMA\" Read goes to mapped range %d.\n", val, debug_r);
#endif
                uint8_t *dma_buf = NULL;
                uint32_t dma_size = 0;
                int32_t dma_idx = -1;
                if (piscsi64_get_dma_window(cfg, &dma_buf, &dma_size, &dma_idx) == 0 &&
                    get_mapped_item_by_address(cfg, piscsi64_u32[2]) == dma_idx) {
#ifdef PISCSI64_DEBUG
                    DEBUG_TRIVIAL("[PISCSI64-%d] Using piscsi64_dma window map %d (size=%u) for READ.\n",
                                  val, dma_idx, dma_size);
#endif
                    uint32_t remaining = piscsi64_u32[1];
                    uint32_t dst_addr = piscsi64_u32[2];
                    ssize_t total_read = 0;
                    int success = 1;
                    while (remaining) {
                        uint32_t chunk = remaining < dma_size ? remaining : dma_size;
                        uint8_t *dst = NULL;
                        uint32_t dst_avail = 0;
                        int rc = piscsi64_get_map_bounds(cfg, dst_addr, chunk, &dst, &dst_avail);
                        if (rc != 0) {
                            DEBUG("[PISCSI64-IO-ERROR] Unit:%d READ refused: DMA range overflow at 0x%08X len=%u\n",
                                  val, dst_addr, chunk);
                            success = 0;
                            break;
                        }
                        ssize_t bytes_read = piscsi64_dev_read(d, dma_buf, chunk);
                        if (bytes_read < 0) {
                            DEBUG("[PISCSI64-IO-ERROR] Unit:%d READ failed: bytes_requested=%u, bytes_read=%zd, errno=%d\n",
                                  val, chunk, bytes_read, errno);
                            success = 0;
                            break;
                        }
                        memcpy(dst, dma_buf, (size_t)bytes_read);
                        total_read += bytes_read;
                        if ((uint32_t)bytes_read != chunk) {
                            DEBUG("[PISCSI64-IO-WARN] Unit:%d PARTIAL READ: requested=%u, actual=%zd\n",
                                  val, chunk, bytes_read);
                            break;
                        }
                        remaining -= chunk;
                        dst_addr += chunk;
                    }
                    if (success) {
                        DEBUG("[PISCSI64-IO-SUCCESS] Unit:%d READ: %zd bytes OK\n", val, total_read);
                    }
                } else {
                    ssize_t bytes_read = piscsi64_dev_read(d, map, piscsi64_u32[1]);
                    if (bytes_read < 0) {
                        DEBUG("[PISCSI64-IO-ERROR] Unit:%d READ failed: bytes_requested=%d, bytes_read=%zd, errno=%d\n", val, piscsi64_u32[1], bytes_read, errno);
                    } else if (bytes_read != (ssize_t)piscsi64_u32[1]) {
                        DEBUG("[PISCSI64-IO-WARN] Unit:%d PARTIAL READ: requested=%d, actual=%zd\n", val, piscsi64_u32[1], bytes_read);
                    } else {
                        DEBUG("[PISCSI64-IO-SUCCESS] Unit:%d READ: %zd bytes OK\n", val, bytes_read);
                    }
                }
            }
            else if (map_rc_read == -2) {
                DEBUG("[PISCSI64-IO-ERROR] Unit:%d READ refused: DMA range overflow at 0x%08X len=%u\n",
                      val, piscsi64_u32[2], piscsi64_u32[1]);
            }
            else {
                DEBUG_TRIVIAL("[PISCSI64-%d] No mapped range found for read.\n", val);
                uint8_t io_buf[PISCSI64_FALLBACK_IO_CHUNK];
                int success = 1;
                uint32_t remaining = piscsi64_u32[1];
                uint32_t dst_addr = piscsi64_u32[2];
                uint32_t total_read = 0;
                while (remaining > 0) {
                    uint32_t chunk = (remaining > PISCSI64_FALLBACK_IO_CHUNK)
                                     ? PISCSI64_FALLBACK_IO_CHUNK
                                     : remaining;
                    ssize_t got = piscsi64_dev_read(d, io_buf, chunk);
                    if (got <= 0) {
                        DEBUG("[PISCSI64-IO-ERROR] Unit:%d FALLBACK READ failed at offset 0x%X: got=%zd errno=%d\n",
                              val, dst_addr, got, errno);
                        success = 0;
                        break;
                    }
                    for (uint32_t i = 0; i < (uint32_t)got; i++) {
                        m68k_write_memory_8(dst_addr + i, (uint32_t)io_buf[i]);
                    }
                    dst_addr += (uint32_t)got;
                    total_read += (uint32_t)got;
                    remaining -= (uint32_t)got;
                    if ((uint32_t)got != chunk) {
                        DEBUG("[PISCSI64-IO-WARN] Unit:%d FALLBACK PARTIAL READ: requested=%u actual=%zd\n",
                              val, chunk, got);
                        break;
                    }
                }
                if (success) {
                    DEBUG("[PISCSI64-IO-SUCCESS] Unit:%d FALLBACK READ: %u bytes OK\n", val, total_read);
                }
            }
            break;
        case PISCSI64_CMD_WRITE64:
        case PISCSI64_CMD_WRITE:
        case PISCSI64_CMD_WRITEBYTES:
            if (val >= PISCSI64_NUM_UNITS) {
                DEBUG("[!!!PISCSI64] BUG: Attempted write to invalid drive %u.\n", val);
                break;
            }
            d = &piscsi64_devs[val];
            if (d->fd == -1) {
                DEBUG ("[PISCSI64] BUG: Attempted write to unmapped drive %d.\n", val);
                break;
            }
            if (!piscsi64_probe_media_online(d)) {
                DEBUG("[PISCSI64] WRITE refused: unit %d is offline.\n", val);
                break;
            }
            if (d->read_only) {
                DEBUG("[PISCSI64] Ignoring write to read-only unit %d.\n", val);
                break;
            }

            if (cmd == PISCSI64_CMD_WRITEBYTES) {
                uint32_t src = piscsi64_u32[0];
                uint32_t block = src / d->block_size;
                d->lba = block;
                DEBUG("[PISCSI64-IO] Unit:%d CMD:WRITEBYTES io_Offset:0x%X io_Length:%d LBA:0x%X file_offset:0x%X from_addr:0x%.8X\n", val, src, piscsi64_u32[1], block, src, piscsi64_u32[2]);
                piscsi64_dev_seek(d, (off64_t)src, SEEK_SET);
            }
            else if (cmd == PISCSI64_CMD_WRITE) {
                uint32_t block = piscsi64_u32[0];
                uint64_t file_offset = (uint64_t)block * d->block_size;
                d->lba = block;
                DEBUG("[PISCSI64-IO] Unit:%d CMD:WRITE io_Offset:0x%X io_Length:%d LBA:0x%X file_offset:0x%llX from_addr:0x%.8X\n", val, block, piscsi64_u32[1], block, (unsigned long long)file_offset, piscsi64_u32[2]);
                piscsi64_dev_seek(d, (off64_t)file_offset, SEEK_SET);
            }
            else {
                uint64_t src = ((uint64_t)piscsi64_u32[3] << 32) | piscsi64_u32[0];
                uint32_t block = (uint32_t)(src / d->block_size);
                d->lba = block;
                DEBUG("[PISCSI64-IO] Unit:%d CMD:WRITE64 io_Offset:0x%llX io_Length:%d LBA:0x%X file_offset:0x%llX from_addr:0x%.8X\n", val, (unsigned long long)src, piscsi64_u32[1], block, (unsigned long long)src, piscsi64_u32[2]);
                piscsi64_dev_seek(d, (off64_t)src, SEEK_SET);
            }

            uint32_t avail_w = 0;
            map = NULL;
            int map_rc_write = piscsi64_get_map_bounds(cfg, piscsi64_u32[2], piscsi64_u32[1], &map, &avail_w);
            if (map_rc_write == 0) {
#ifdef PISCSI64_DEBUG
                int32_t debug_r = get_mapped_item_by_address(cfg, piscsi64_u32[2]);
                DEBUG_TRIVIAL("[PISCSI64-%d] \"DMA\" Write comes from mapped range %d.\n", val, debug_r);
#endif
                uint8_t *dma_buf = NULL;
                uint32_t dma_size = 0;
                int32_t dma_idx = -1;
                if (piscsi64_get_dma_window(cfg, &dma_buf, &dma_size, &dma_idx) == 0 &&
                    get_mapped_item_by_address(cfg, piscsi64_u32[2]) == dma_idx) {
#ifdef PISCSI64_DEBUG
                    DEBUG_TRIVIAL("[PISCSI64-%d] Using piscsi64_dma window map %d (size=%u) for WRITE.\n",
                                  val, dma_idx, dma_size);
#endif
                    uint32_t remaining = piscsi64_u32[1];
                    uint32_t src_addr = piscsi64_u32[2];
                    ssize_t total_written = 0;
                    int success = 1;
                    while (remaining) {
                        uint32_t chunk = remaining < dma_size ? remaining : dma_size;
                        uint8_t *src_ptr = NULL;
                        uint32_t src_avail = 0;
                        int rc = piscsi64_get_map_bounds(cfg, src_addr, chunk, &src_ptr, &src_avail);
                        if (rc != 0) {
                            DEBUG("[PISCSI64-IO-ERROR] Unit:%d WRITE refused: DMA range overflow at 0x%08X len=%u\n",
                                  val, src_addr, chunk);
                            success = 0;
                            break;
                        }
                        memcpy(dma_buf, src_ptr, chunk);
                        ssize_t bytes_written = piscsi64_dev_write(d, dma_buf, chunk);
                        if (bytes_written < 0) {
                            DEBUG("[PISCSI64-IO-ERROR] Unit:%d WRITE failed: bytes_requested=%u, bytes_written=%zd, errno=%d\n",
                                  val, chunk, bytes_written, errno);
                            success = 0;
                            break;
                        }
                        total_written += bytes_written;
                        if ((uint32_t)bytes_written != chunk) {
                            DEBUG("[PISCSI64-IO-WARN] Unit:%d PARTIAL WRITE: requested=%u, actual=%zd\n",
                                  val, chunk, bytes_written);
                            break;
                        }
                        remaining -= chunk;
                        src_addr += chunk;
                    }
                    if (success) {
                        DEBUG("[PISCSI64-IO-SUCCESS] Unit:%d WRITE: %zd bytes OK\n", val, total_written);
                    }
                } else {
                    ssize_t bytes_written = piscsi64_dev_write(d, map, piscsi64_u32[1]);
                    if (bytes_written < 0) {
                        DEBUG("[PISCSI64-IO-ERROR] Unit:%d WRITE failed: bytes_requested=%d, bytes_written=%zd, errno=%d\n", val, piscsi64_u32[1], bytes_written, errno);
                    } else if (bytes_written != (ssize_t)piscsi64_u32[1]) {
                        DEBUG("[PISCSI64-IO-WARN] Unit:%d PARTIAL WRITE: requested=%d, actual=%zd\n", val, piscsi64_u32[1], bytes_written);
                    } else {
                        DEBUG("[PISCSI64-IO-SUCCESS] Unit:%d WRITE: %zd bytes OK\n", val, bytes_written);
                    }
                }
            }
            else if (map_rc_write == -2) {
                DEBUG("[PISCSI64-IO-ERROR] Unit:%d WRITE refused: DMA range overflow at 0x%08X len=%u\n",
                      val, piscsi64_u32[2], piscsi64_u32[1]);
            }
            else {
                DEBUG_TRIVIAL("[PISCSI64-%d] No mapped range found for write.\n", val);
                uint8_t io_buf[PISCSI64_FALLBACK_IO_CHUNK];
                int success = 1;
                uint32_t remaining = piscsi64_u32[1];
                uint32_t src_addr = piscsi64_u32[2];
                uint32_t total_written = 0;
                while (remaining > 0) {
                    uint32_t chunk = (remaining > PISCSI64_FALLBACK_IO_CHUNK)
                                     ? PISCSI64_FALLBACK_IO_CHUNK
                                     : remaining;
                    for (uint32_t i = 0; i < chunk; i++) {
                        io_buf[i] = (uint8_t)m68k_read_memory_8(src_addr + i);
                    }
                    ssize_t wrote = piscsi64_dev_write(d, io_buf, chunk);
                    if (wrote <= 0) {
                        DEBUG("[PISCSI64-IO-ERROR] Unit:%d FALLBACK WRITE failed at offset 0x%X: wrote=%zd errno=%d\n",
                              val, src_addr, wrote, errno);
                        success = 0;
                        break;
                    }
                    src_addr += (uint32_t)wrote;
                    total_written += (uint32_t)wrote;
                    remaining -= (uint32_t)wrote;
                    if ((uint32_t)wrote != chunk) {
                        DEBUG("[PISCSI64-IO-WARN] Unit:%d FALLBACK PARTIAL WRITE: requested=%u actual=%zd\n",
                              val, chunk, wrote);
                        break;
                    }
                }
                if (success) {
                    DEBUG("[PISCSI64-IO-SUCCESS] Unit:%d FALLBACK WRITE: %u bytes OK\n", val, total_written);
                }
            }
            break;
        case PISCSI64_CMD_ADDR1: case PISCSI64_CMD_ADDR2: case PISCSI64_CMD_ADDR3: case PISCSI64_CMD_ADDR4: {
            int addr_idx = ((addr & 0xFFFF) - PISCSI64_CMD_ADDR1) / 4;
            piscsi64_u32[addr_idx] = val;
            break;
        }
        case PISCSI64_CMD_DRVNUM:
            if (val >= PISCSI64_NUM_UNITS) {
                piscsi64_cur_drive = 255;
            }
            else {
                piscsi64_cur_drive = (uint8_t)val;
            }
            if (piscsi64_cur_drive != 255) {
                DEBUG("[PISCSI64] (%s) Drive number set to %d (%d)\n", op_type_names[type], piscsi64_cur_drive, val);
            }
            break;
        case PISCSI64_CMD_DRVNUMX:
            if (val >= PISCSI64_NUM_UNITS) {
                piscsi64_cur_drive = 255;
                DEBUG("[!!!PISCSI64] DRVNUMX out of range: %u.\n", val);
            } else {
                piscsi64_cur_drive = (uint8_t)val;
                DEBUG("[PISCSI64] DRVNUMX: %d.\n", val);
            }
            break;
        case PISCSI64_CMD_MEDIA_EJECT:
            if (val >= PISCSI64_NUM_UNITS) {
                DEBUG("[PISCSI64] MEDIA_EJECT out of range: %u.\n", val);
                break;
            }
            (void)piscsi64_media_eject((uint8_t)val);
            break;
        case PISCSI64_CMD_MEDIA_INSERT:
            if (val >= PISCSI64_NUM_UNITS) {
                DEBUG("[PISCSI64] MEDIA_INSERT out of range: %u.\n", val);
                break;
            }
            (void)piscsi64_media_insert((uint8_t)val);
            break;
        case PISCSI64_CMD_DEBUGME:
            piscsi64_debugme(val);
            break;
        case PISCSI64_CMD_DRIVER:
            DEBUG("[PISCSI64] Driver copy/patch called, destination address %.8X.\n", val);
            int32_t driver_r = get_mapped_item_by_address(cfg, val);
            if (driver_r != -1) {
                uint32_t driver_base_addr = (uint32_t)(val - cfg->map_offset[driver_r]);
                uint8_t *dst_data = cfg->map_data[driver_r];
                uint8_t cur_partition = 0;
                memcpy(dst_data + driver_base_addr, piscsi64_rom_ptr + PISCSI64_DRIVER_OFFSET, 0x4000 - PISCSI64_DRIVER_OFFSET);

                piscsi64_hinfo.base_offset = val;

                if (reloc_hunks(piscsi64_hreloc, dst_data + driver_base_addr, &piscsi64_hinfo) != 0) {
                    LOG_ERROR("[PISCSI64] Driver relocation failed; aborting handler install\n");
                    break;
                }

                #define PUTNODELONG(val) do { uint32_t temp = htobe32(val); memcpy(&dst_data[p_offs], &temp, sizeof(temp)); p_offs += 4; } while(0)
                #define PUTNODELONGBE(val) do { uint32_t temp = val; memcpy(&dst_data[p_offs], &temp, sizeof(temp)); p_offs += 4; } while(0)

                for (int i = 0; i < 128; i++) {
                    piscsi64_rom_partitions[i] = 0;
                    piscsi64_rom_partition_prio[i] = 0;
                    piscsi64_rom_partition_dostype[i] = 0;
                }
                piscsi64_rom_cur_partition = 0;

                uint32_t driver_data_addr = driver_base_addr + 0x3F00;
                sprintf((char *)dst_data + driver_data_addr, "pi-scsi64.device");
                uint32_t driver_addr2 = driver_base_addr + 0x4000;
                for (int i = 0; i < PISCSI64_NUM_UNITS; i++) {
                    if (piscsi64_devs[i].fd == -1)
                        goto skip_disk;

                    if (piscsi64_devs[i].num_partitions) {
                        uint32_t p_offs = driver_addr2;
                        DEBUG("[PISCSI64] Adding %d partitions for unit %d\n", piscsi64_devs[i].num_partitions, i);
                        for (uint32_t j = 0; j < piscsi64_devs[i].num_partitions; j++) {
                            DEBUG("Partition %d: %s\n", j, piscsi64_devs[i].pb[j]->pb_DriveName + 1);
                            sprintf((char *)dst_data + p_offs, "%s", piscsi64_devs[i].pb[j]->pb_DriveName + 1);
                            p_offs += 0x20;
                            PUTNODELONG(driver_addr2 + cfg->map_offset[driver_r]);
                            PUTNODELONG(driver_data_addr + cfg->map_offset[driver_r]);
                            PUTNODELONG(i);
                            PUTNODELONG(0);
                            uint32_t nodesize = (be32toh(piscsi64_devs[i].pb[j]->pb_Environment[0]) + 1) * 4;
                            memcpy(dst_data + p_offs, piscsi64_devs[i].pb[j]->pb_Environment, nodesize);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
                            /*
                             * Cast alignment warning intentionally suppressed:
                             * PiSCSI interprets Amiga-side structures that arrive as byte streams.
                             * On the Pi side these are represented as C structs with 32-bit alignment.
                             * The external contract guarantees these blocks are properly aligned as expected by the Amiga.
                             */
                            struct pihd_dosnode_data *dat = (struct pihd_dosnode_data *)((char *)(&dst_data[driver_addr2+0x20]));
#pragma GCC diagnostic pop

                            if (BE(piscsi64_devs[i].pb[j]->pb_Flags) & 0x01) {
                                DEBUG("Partition is bootable.\n");
                                piscsi64_rom_partition_prio[cur_partition] = BE(dat->priority);
                            }
                            else {
                                DEBUG("Partition is not bootable.\n");
                                piscsi64_rom_partition_prio[cur_partition] = (uint32_t)-128;
                            }

                            DEBUG("DOSNode Data:\n");
                            DEBUG("Name: %s Device: %s\n", dst_data + driver_addr2, dst_data + driver_data_addr);
                            DEBUG("Unit: %d Flags: %d Pad1: %d\n", BE(dat->unit), BE(dat->flags), BE(dat->pad1));
                            DEBUG("Node len: %d Block len: %d\n", BE(dat->node_len) * 4, BE(dat->block_len) * 4);
                            DEBUG("H: %d SPB: %d BPS: %d\n", BE(dat->surf), BE(dat->secs_per_block), BE(dat->blocks_per_track));
                            DEBUG("Reserved: %d Prealloc: %d\n", BE(dat->reserved_blocks), BE(dat->pad2));
                            DEBUG("Interleaved: %d Buffers: %d Memtype: %d\n", BE(dat->interleave), BE(dat->buffers), BE(dat->mem_type));
                            DEBUG("Lowcyl: %d Highcyl: %d Prio: %d\n", BE(dat->lowcyl), BE(dat->highcyl), BE(dat->priority));
                            DEBUG("Maxtransfer: %.8X Mask: %.8X\n", BE(dat->maxtransfer), BE(dat->transfer_mask));
                            DEBUG("DOSType: %.8X\n", BE(dat->dostype));

                            piscsi64_rom_partitions[cur_partition] = (uint32_t)(driver_addr2 + 0x20 + cfg->map_offset[driver_r]);
                            piscsi64_rom_partition_dostype[cur_partition] = dat->dostype;
                            cur_partition++;
                            driver_addr2 += 0x100;
                            p_offs = driver_addr2;
                        }
                    }
skip_disk:;
                }
            }

            break;
        case PISCSI64_CMD_NEXTPART:
            DEBUG("[PISCSI64] Switch partition %d -> %d\n", piscsi64_rom_cur_partition, piscsi64_rom_cur_partition + 1);
            if (piscsi64_rom_cur_partition < 127) {
                piscsi64_rom_cur_partition++;
            }
            break;
        case PISCSI64_CMD_NEXTFS:
            DEBUG("[PISCSI64] Switch file file system %d -> %d\n", piscsi64_rom_cur_fs, piscsi64_rom_cur_fs + 1);
            if (piscsi64_rom_cur_fs < piscsi64_num_fs) {
                piscsi64_rom_cur_fs++;
            }
            break;
        case PISCSI64_CMD_COPYFS:
            DEBUG("[PISCSI64] Copy file system %d to %.8X and reloc.\n", piscsi64_rom_cur_fs, piscsi64_u32[2]);
            if (piscsi64_rom_cur_fs >= piscsi64_num_fs ||
                piscsi64_filesystems[piscsi64_rom_cur_fs].binary_data == NULL ||
                !piscsi64_filesystems[piscsi64_rom_cur_fs].valid) {
                DEBUG("[PISCSI64] COPYFS ignored for invalid fs index %u (num_fs=%u).\n",
                      (unsigned int)piscsi64_rom_cur_fs, (unsigned int)piscsi64_num_fs);
                break;
            }
            int32_t copy_r = get_mapped_item_by_address(cfg, piscsi64_u32[2]);
            if (copy_r != -1) {
                uint32_t copy_base_addr = (uint32_t)(piscsi64_u32[2] - cfg->map_offset[copy_r]);
                memcpy(cfg->map_data[copy_r] + copy_base_addr, piscsi64_filesystems[piscsi64_rom_cur_fs].binary_data, piscsi64_filesystems[piscsi64_rom_cur_fs].h_info.byte_size);
                piscsi64_filesystems[piscsi64_rom_cur_fs].h_info.base_offset = piscsi64_u32[2];
                if (reloc_hunks(piscsi64_filesystems[piscsi64_rom_cur_fs].relocs, cfg->map_data[copy_r] + copy_base_addr,
                                &piscsi64_filesystems[piscsi64_rom_cur_fs].h_info) != 0) {
                    char *dosID = (char *)&piscsi64_filesystems[piscsi64_rom_cur_fs].FS_ID;
                    LOG_ERROR("[PISCSI64] Rejecting filesystem %c%c%c/%d: relocation failed\n",
                              dosID[0], dosID[1], dosID[2], dosID[3]);
                    piscsi64_filesystems[piscsi64_rom_cur_fs].handler = 0;
                    piscsi64_filesystems[piscsi64_rom_cur_fs].valid = 0;
                    break;
                }
                piscsi64_filesystems[piscsi64_rom_cur_fs].handler = piscsi64_u32[2];
                piscsi64_filesystems[piscsi64_rom_cur_fs].valid = 1;
                {
                    char *dosID = (char *)&piscsi64_filesystems[piscsi64_rom_cur_fs].FS_ID;
                    if (!fs_handler_valid(&piscsi64_filesystems[piscsi64_rom_cur_fs], piscsi64_filesystems[piscsi64_rom_cur_fs].handler,
                                          (uint8_t)piscsi64_rom_cur_partition, dosID)) {
                        piscsi64_filesystems[piscsi64_rom_cur_fs].handler = 0;
                        piscsi64_filesystems[piscsi64_rom_cur_fs].valid = 0;
                    }
                }
            }
            break;
        case PISCSI64_CMD_SETFSH: {
            int fs_idx = 0;
            DEBUG("[PISCSI64] Set handler for partition %d (DeviceNode: %.8X)\n", piscsi64_rom_cur_partition, val);
            int32_t setfsh_r = get_mapped_item_by_address(cfg, val);
            if (setfsh_r != -1) {
                uint32_t setfsh_base_addr = (uint32_t)(val - cfg->map_offset[setfsh_r]);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
                /*
                 * Cast alignment warning intentionally suppressed:
                 * PiSCSI interprets Amiga-side structures that arrive as byte streams.
                 * On the Pi side these are represented as C structs with 32-bit alignment.
                 * The external contract guarantees these blocks are properly aligned as expected by the Amiga.
                 */
                struct DeviceNode *node = (struct DeviceNode *)((char *)(cfg->map_data[setfsh_r] + setfsh_base_addr));
#pragma GCC diagnostic pop
                char *dosID = (char *)&piscsi64_rom_partition_dostype[piscsi64_rom_cur_partition];

                DEBUG("[PISCSI64] Partition DOSType is %c%c%c/%d\n", dosID[0], dosID[1], dosID[2], dosID[3]);
                // First try exact match
                for (fs_idx = 0; fs_idx < piscsi64_num_fs; fs_idx++) {
                    if (piscsi64_rom_partition_dostype[piscsi64_rom_cur_partition] == piscsi64_filesystems[fs_idx].FS_ID) {
                        if (fs_handler_valid(&piscsi64_filesystems[fs_idx], piscsi64_filesystems[fs_idx].handler,
                                             (uint8_t)piscsi64_rom_cur_partition, dosID)) {
                            node->dn_SegList = htobe32((((piscsi64_filesystems[fs_idx].handler) +
                                                         piscsi64_filesystems[fs_idx].h_info.header_size) >>
                                                        2));
                            node->dn_GlobalVec = 0xFFFFFFFF;
                            goto fs_found;
                        }
                        LOG_ERROR("[PISCSI64] Handler rejected for %c%c%c/%d (partition %u)\n",
                                  dosID[0], dosID[1], dosID[2], dosID[3], piscsi64_rom_cur_partition);
                        goto fs_not_found;
                    }
                }

                // If no exact match, try fallback mappings (e.g., DOS/3 -> DOS/1 for FastFileSystem)
                uint32_t fallback_dostype = piscsi64_rom_partition_dostype[piscsi64_rom_cur_partition];

                // Map DOS/3 (FFS International) to DOS/1 (FFS) handler since they use the same filesystem
                if (fallback_dostype == htobe32(0x444F5303)) { // DOS/3
                    fallback_dostype = htobe32(0x444F5301);     // DOS/1
                    for (fs_idx = 0; fs_idx < piscsi64_num_fs; fs_idx++) {
                        if (fallback_dostype == piscsi64_filesystems[fs_idx].FS_ID) {
                            if (fs_handler_valid(&piscsi64_filesystems[fs_idx], piscsi64_filesystems[fs_idx].handler,
                                                 (uint8_t)piscsi64_rom_cur_partition, dosID)) {
                                node->dn_SegList = htobe32((((piscsi64_filesystems[fs_idx].handler) +
                                                             piscsi64_filesystems[fs_idx].h_info.header_size) >>
                                                            2));
                                node->dn_GlobalVec = 0xFFFFFFFF;
                                DEBUG("[PISCSI64] Fallback: Mapped DOS/3 partition to DOS/1 filesystem handler.\n");
                                goto fs_found;
                            }
                            LOG_ERROR("[PISCSI64] Handler rejected for %c%c%c/%d (partition %u)\n",
                                      dosID[0], dosID[1], dosID[2], dosID[3], piscsi64_rom_cur_partition);
                            goto fs_not_found;
                        }
                    }
                }

                node->dn_GlobalVec = 0xFFFFFFFF;
                node->dn_SegList = 0;
fs_not_found:
                printf("[!!!PISCSI64] Found no valid handler for file system %c%c%c/%d\n", dosID[0], dosID[1], dosID[2], dosID[3]);
fs_found:;
                DEBUG("[FS-HANDLER] Next: %d Type: %.8X\n", BE(node->dn_Next), BE(node->dn_Type));
                DEBUG("[FS-HANDLER] Task: %d Lock: %d\n", BE(node->dn_Task), BE(node->dn_Lock));
                DEBUG("[FS-HANDLER] Handler: %d Stacksize: %d\n", BE(node->dn_Handler), BE(node->dn_StackSize));
                DEBUG("[FS-HANDLER] Priority: %d Startup: %d (%.8X)\n", BE(node->dn_Priority), BE(node->dn_Startup), BE(node->dn_Startup));
                DEBUG("[FS-HANDLER] SegList: %.8X GlobalVec: %d\n", BE((uint32_t)node->dn_SegList), (int)BE(node->dn_GlobalVec));
                if (fs_idx < piscsi64_num_fs) {
                    DEBUG("[PISCSI64] Handler for partition %.8X set to %.8X (%.8X).\n",
                          (uint32_t)BE(node->dn_Name), (uint32_t)BE(piscsi64_filesystems[fs_idx].FS_ID),
                          piscsi64_filesystems[fs_idx].handler);
                } else {
                    DEBUG("[PISCSI64] Handler for partition %.8X not set (no matching FS entry).\n",
                          (uint32_t)BE(node->dn_Name));
                }
            }
            break;
        }
        case PISCSI64_CMD_LOADFS: {
            DEBUG("[PISCSI64] Attempt to load file system for partition %d from disk.\n", piscsi64_rom_cur_partition);
            int32_t mapped_r = get_mapped_item_by_address(cfg, val);
            if (mapped_r != -1) {
                char dosID[4];
                char dosID_str[16];
                memset(dosID_str, 0x00, sizeof(dosID_str));
                uint32_t raw_dostype = piscsi64_rom_partition_dostype[piscsi64_rom_cur_partition];
                if (amiga_fsid_build_dosid(raw_dostype, dosID, dosID_str, sizeof(dosID_str)) != 0) {
                    printf("[FSHD-Late] No mapping for DOSType 0x%08X\n", be32toh(raw_dostype));
                    piscsi64_u32[3] = 0xFFFFFFFF;
                    break;
                }

                piscsi64_filesystems[piscsi64_num_fs].binary_data = NULL;
                piscsi64_filesystems[piscsi64_num_fs].fhb = NULL;
                piscsi64_filesystems[piscsi64_num_fs].FS_ID = raw_dostype;
                piscsi64_filesystems[piscsi64_num_fs].handler = 0;
                piscsi64_filesystems[piscsi64_num_fs].valid = 0;
                if (load_fs(&piscsi64_filesystems[piscsi64_num_fs], dosID) != -1) {
                    printf("[FSHD-Late] Loaded file system %s from fs storage.\n", dosID_str);
                    piscsi64_u32[3] = piscsi64_num_fs;
                    piscsi64_rom_cur_fs = piscsi64_num_fs;
                    piscsi64_filesystems[piscsi64_num_fs].valid = 1;
                    piscsi64_num_fs++;
                } else {
                    printf("[FSHD-Late] Failed to load file system %s from fs storage.\n", dosID_str);
                    piscsi64_u32[3] = 0xFFFFFFFF;
                }
            }
            break;
        }
        case PISCSI64_DBG_VAL1: case PISCSI64_DBG_VAL2: case PISCSI64_DBG_VAL3: case PISCSI64_DBG_VAL4:
        case PISCSI64_DBG_VAL5: case PISCSI64_DBG_VAL6: case PISCSI64_DBG_VAL7: case PISCSI64_DBG_VAL8: {
            int i = ((addr & 0xFFFF) - PISCSI64_DBG_VAL1) / 4;
            piscsi64_dbg[i] = val;
            break;
        }
        case PISCSI64_DBG_MSG:
#ifdef PISCSI64_DEBUG
            print_piscsi64_debug_message((int)val);
#endif
            break;
        default:
            DEBUG("[!!!PISCSI64] WARN: Unhandled %s register write to %.8X: %d\n", op_type_names[type], addr, (int)val);
            break;
    }
}

#define PIB 0x00
#define PISCSI64_ROM_PROGRESS_BYTES (1024u * 4u)
#define PISCSI64_ROM_DOT_CHUNK_OPS 64u

uint32_t handle_piscsi64_read(uint32_t addr, uint8_t type) {
    uint32_t open_bus = 0xFFFFFFFF;
    if (type) {}
    if (type == OP_TYPE_BYTE) {
        open_bus = 0xFF;
    } else if (type == OP_TYPE_WORD) {
        open_bus = 0xFFFF;
    }

    uint16_t cmd = (uint16_t)(addr & 0xFFFF);

    if (cmd >= PISCSI64_CMD_ROM) {
        uint32_t romoffs = cmd - PISCSI64_CMD_ROM;
        if (romoffs < (piscsi64_rom_size + PIB)) {
            if (!rom_read_logged) {
                LOG_INFO("[PISCSI64-ROM] first read type=%s addr=$%.8X romoffs=$%.4X size=$%.4X\n",
                         op_type_names[type], addr, romoffs, piscsi64_rom_size);
                rom_read_logged = 1;
            }
            //DEBUG("[PISCSI64] %s read from Boot ROM @$%.4X (%.8X): ", op_type_names[type], romoffs, addr);
            uint32_t v = 0;
            uint32_t access_size = 1;
            switch (type) {
                case OP_TYPE_BYTE:
                    v = piscsi64_rom_ptr[romoffs - PIB];
                    access_size = 1;
                    //DEBUG("%.2X\n", v);
                    break;
                case OP_TYPE_WORD: {
                    uint16_t temp_val;
                    memcpy(&temp_val, &piscsi64_rom_ptr[romoffs - PIB], sizeof(temp_val));
                    v = be16toh(temp_val);
                    access_size = 2;
                    //DEBUG("%.4X\n", v);
                    break;
                }
                case OP_TYPE_LONGWORD: {
                    uint32_t temp_val;
                    memcpy(&temp_val, &piscsi64_rom_ptr[romoffs - PIB], sizeof(temp_val));
                    v = be32toh(temp_val);
                    access_size = 4;
                    //DEBUG("%.8X\n", v);
                    break;
                }
            }

            rom_read_total_bytes += access_size;
            rom_read_bytes_since_log += access_size;
            rom_read_total_ops++;
            if (rom_read_dot_trace) {
                fputc('.', stdout);
                rom_read_ops_since_line++;
                if (rom_read_ops_since_line >= PISCSI64_ROM_DOT_CHUNK_OPS) {
                    printf(" [PISCSI64-ROM] ops=%u bytes=%u last_offs=$%.4X type=%s val=$%.8X\n",
                           rom_read_total_ops, rom_read_total_bytes, romoffs, op_type_names[type], v);
                    rom_read_ops_since_line = 0;
                }
                fflush(stdout);
            }
            if (rom_read_progress_trace && rom_read_bytes_since_log >= PISCSI64_ROM_PROGRESS_BYTES) {
                LOG_DEBUG("[PISCSI64-ROM] progress bytes=%u romoffs=$%.4X type=%s val=$%.8X\n",
                          rom_read_total_bytes, romoffs, op_type_names[type], v);
                rom_read_bytes_since_log = 0;
            }
            return v;
        }
        return open_bus;
    }

    if (piscsi64_cur_drive >= PISCSI64_NUM_UNITS) {
        switch (cmd) {
            case PISCSI64_CMD_DRVTYPE:
            case PISCSI64_CMD_CYLS:
            case PISCSI64_CMD_HEADS:
            case PISCSI64_CMD_SECS:
            case PISCSI64_CMD_BLOCKS:
            case PISCSI64_CMD_BLOCKSIZE:
                DEBUG("[!!!PISCSI64] Read from %s with invalid current drive %u.\n",
                      op_type_names[type], (unsigned int)piscsi64_cur_drive);
                return open_bus;
            default:
                break;
        }
    }

    switch (cmd) {
        /*
         * These command offsets are write-driven data paths.
         * Some systems still probe them with reads during board/device setup;
         * treat as open-bus and do not spam warnings.
         */
        case PISCSI64_CMD_WRITE:
        case PISCSI64_CMD_READ:
        case PISCSI64_CMD_MEDIA_EJECT:
        case PISCSI64_CMD_MEDIA_INSERT:
            return open_bus;
        case PISCSI64_CMD_ADDR1: case PISCSI64_CMD_ADDR2: case PISCSI64_CMD_ADDR3: case PISCSI64_CMD_ADDR4: {
            int i = (cmd - PISCSI64_CMD_ADDR1) / 4;
            return piscsi64_u32[i];
            break;
        }
        case PISCSI64_CMD_DRVTYPE:
            (void)piscsi64_probe_media_online(&piscsi64_devs[piscsi64_cur_drive]);
            if (piscsi64_devs[piscsi64_cur_drive].fd == -1) {
                DEBUG("[PISCSI64] %s Read from DRVTYPE %d, drive not attached.\n", op_type_names[type], piscsi64_cur_drive);
                return 0;
            }
            DEBUG("[PISCSI64] %s Read from DRVTYPE %d: kind=%u ro=%u\n",
                  op_type_names[type], piscsi64_cur_drive,
                  (unsigned int)piscsi64_devs[piscsi64_cur_drive].media_kind,
                  (unsigned int)piscsi64_devs[piscsi64_cur_drive].read_only);
            return PISCSI64_DRVTYPE_BUILD(
                piscsi64_media_scsi_type((enum piscsi64_media_kind)piscsi64_devs[piscsi64_cur_drive].media_kind),
                piscsi64_devs[piscsi64_cur_drive].read_only);
            break;
        case PISCSI64_CMD_DRVNUM:
            return piscsi64_cur_drive;
            break;
        case PISCSI64_CMD_CYLS:
            (void)piscsi64_probe_media_online(&piscsi64_devs[piscsi64_cur_drive]);
            if (piscsi64_devs[piscsi64_cur_drive].fd == -1) {
                return 0;
            }
            DEBUG("[PISCSI64] %s Read from CYLS %d: %d\n", op_type_names[type], piscsi64_cur_drive, piscsi64_devs[piscsi64_cur_drive].c);
            return piscsi64_devs[piscsi64_cur_drive].c;
            break;
        case PISCSI64_CMD_HEADS:
            (void)piscsi64_probe_media_online(&piscsi64_devs[piscsi64_cur_drive]);
            if (piscsi64_devs[piscsi64_cur_drive].fd == -1) {
                return 0;
            }
            DEBUG("[PISCSI64] %s Read from HEADS %d: %d\n", op_type_names[type], piscsi64_cur_drive, piscsi64_devs[piscsi64_cur_drive].h);
            return piscsi64_devs[piscsi64_cur_drive].h;
            break;
        case PISCSI64_CMD_SECS:
            (void)piscsi64_probe_media_online(&piscsi64_devs[piscsi64_cur_drive]);
            if (piscsi64_devs[piscsi64_cur_drive].fd == -1) {
                return 0;
            }
            DEBUG("[PISCSI64] %s Read from SECS %d: %d\n", op_type_names[type], piscsi64_cur_drive, piscsi64_devs[piscsi64_cur_drive].s);
            return piscsi64_devs[piscsi64_cur_drive].s;
            break;
        case PISCSI64_CMD_BLOCKS: {
            (void)piscsi64_probe_media_online(&piscsi64_devs[piscsi64_cur_drive]);
            if (piscsi64_devs[piscsi64_cur_drive].fd == -1) {
                return 0;
            }
            if (piscsi64_devs[piscsi64_cur_drive].block_size == 0) {
                return 0;
            }
            uint32_t blox = (uint32_t)(piscsi64_devs[piscsi64_cur_drive].fs / piscsi64_devs[piscsi64_cur_drive].block_size);
            DEBUG("[PISCSI64] %s Read from BLOCKS %d: %d\n", op_type_names[type], piscsi64_cur_drive, (uint32_t)(piscsi64_devs[piscsi64_cur_drive].fs / piscsi64_devs[piscsi64_cur_drive].block_size));
            DEBUG("fs: %llu (%d)\n", (unsigned long long)piscsi64_devs[piscsi64_cur_drive].fs, blox);
            return blox;
            break;
        }
        case PISCSI64_CMD_GETPART: {
            if (piscsi64_rom_cur_partition >= 128) {
                return 0;
            }
            DEBUG("[PISCSI64] Get ROM partition %d offset: %.8X\n", piscsi64_rom_cur_partition, piscsi64_rom_partitions[piscsi64_rom_cur_partition]);
            return piscsi64_rom_partitions[piscsi64_rom_cur_partition];
            break;
        }
        case PISCSI64_CMD_GETPRIO:
            if (piscsi64_rom_cur_partition >= 128) {
                return 0;
            }
            DEBUG("[PISCSI64] Get partition %d boot priority: %d\n", piscsi64_rom_cur_partition, piscsi64_rom_partition_prio[piscsi64_rom_cur_partition]);
            return piscsi64_rom_partition_prio[piscsi64_rom_cur_partition];
            break;
        case PISCSI64_CMD_CHECKFS: {
            if (piscsi64_rom_cur_fs >= piscsi64_num_fs) {
                return 0;
            }
            /* FS_ID is stored in on-disk big-endian byte order; MMIO must return CPU-order value. */
            uint32_t fs_id = (uint32_t)BE(piscsi64_filesystems[piscsi64_rom_cur_fs].FS_ID);
            DEBUG("[PISCSI64] Get current loaded file system: %.8X\n", fs_id);
            return fs_id;
        }
        case PISCSI64_CMD_FSSIZE:
            if (piscsi64_rom_cur_fs >= piscsi64_num_fs) {
                return 0;
            }
            DEBUG("[PISCSI64] Get alloc size of loaded file system: %d\n", piscsi64_filesystems[piscsi64_rom_cur_fs].h_info.alloc_size);
            return piscsi64_filesystems[piscsi64_rom_cur_fs].h_info.alloc_size;
        case PISCSI64_CMD_BLOCKSIZE:
            (void)piscsi64_probe_media_online(&piscsi64_devs[piscsi64_cur_drive]);
            if (piscsi64_devs[piscsi64_cur_drive].fd == -1) {
                return 0;
            }
            DEBUG("[PISCSI64] Get block size of drive %d: %d\n", piscsi64_cur_drive, piscsi64_devs[piscsi64_cur_drive].block_size);
            return piscsi64_devs[piscsi64_cur_drive].block_size;
        case PISCSI64_CMD_GET_FS_INFO: {
            int fs_idx = 0;
            uint32_t val = piscsi64_u32[1];
            int32_t r = get_mapped_item_by_address(cfg, val);
            if (r != -1) {
#ifdef PISCSI64_DEBUG
                char *dosID = (char *)&piscsi64_rom_partition_dostype[piscsi64_rom_cur_partition];
                DEBUG("[PISCSI64-GET-FS-INFO] Partition DOSType is %c%c%c/%d\n", dosID[0], dosID[1], dosID[2], dosID[3]);
#endif
                // First try exact match
                for (fs_idx = 0; fs_idx < piscsi64_num_fs; fs_idx++) {
                    if (piscsi64_rom_partition_dostype[piscsi64_rom_cur_partition] == piscsi64_filesystems[fs_idx].FS_ID) {
                        return 0;
                    }
                }

                // If no exact match, try fallback mappings (e.g., DOS/3 -> DOS/1 for FastFileSystem)
                uint32_t fallback_dostype = piscsi64_rom_partition_dostype[piscsi64_rom_cur_partition];

                // Map DOS/3 (FFS International) to DOS/1 (FFS) handler since they use the same filesystem
                if (fallback_dostype == htobe32(0x444F5303)) { // DOS/3
                    fallback_dostype = htobe32(0x444F5301);     // DOS/1
                    for (fs_idx = 0; fs_idx < piscsi64_num_fs; fs_idx++) {
                        if (fallback_dostype == piscsi64_filesystems[fs_idx].FS_ID) {
                            DEBUG("[PISCSI64-GET-FS-INFO] Fallback: Mapped DOS/3 partition to DOS/1 filesystem handler.\n");
                            return 0;
                        }
                    }
                }
            }
            return 1;
        }
        default:
            DEBUG("[!!!PISCSI64] WARN: Unhandled %s register read from %.8X\n", op_type_names[type], addr);
            break;
    }

    return open_bus;
}
