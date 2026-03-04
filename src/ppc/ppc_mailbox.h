#ifndef PPC_MAILBOX_H
#define PPC_MAILBOX_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

/*
 * Mailbox page mapping inside PPC RAM.
 * Both host and PPC touch the same backing memory.
 */
#define PPC_MAILBOX_PPC_ADDR 0x00002000U
#define PPC_MAILBOX_SIZE 0x00001000U

#define PPC_MAILBOX_MAGIC 0x504d4241U /* "PMBA" */
#define PPC_MAILBOX_VERSION 1U

#define PPC_MAILBOX_PAYLOAD_BYTES 512U
#define PPC_MAILBOX_PAYLOAD_IN_OFFSET 0U
#define PPC_MAILBOX_PAYLOAD_OUT_OFFSET 256U
#define PPC_MAILBOX_MEMCPY32_MAX_WORDS 64U

/*
 * Shared ordering primitive for host-side mailbox updates.
 * PPC firmware uses `sync` at corresponding publication points.
 */
#define PPC_MAILBOX_MEMORY_BARRIER() atomic_thread_fence(memory_order_seq_cst)

#if defined(__GNUC__)
#define PPC_MAILBOX_PACKED __attribute__((packed))
#else
#define PPC_MAILBOX_PACKED
#endif

typedef enum ppc_mailbox_command {
    PPC_MAILBOX_CMD_NONE = 0U,
    PPC_MAILBOX_CMD_PING = 1U,
    PPC_MAILBOX_CMD_MEMCPY32 = 2U,
    PPC_MAILBOX_CMD_HOST_TIME32 = 3U,
    PPC_MAILBOX_CMD_MEM_CRC32 = 4U
} ppc_mailbox_command;

typedef enum ppc_mailbox_status {
    PPC_MAILBOX_STATUS_IDLE = 0U,
    PPC_MAILBOX_STATUS_BUSY = 1U,
    PPC_MAILBOX_STATUS_DONE = 2U,
    PPC_MAILBOX_STATUS_ERR = 3U,
    PPC_MAILBOX_STATUS_RANGE = 4U
} ppc_mailbox_status;

typedef enum ppc_mailbox_host_command {
    PPC_MAILBOX_HOST_CMD_NONE = 0U,
    PPC_MAILBOX_HOST_CMD_TIME32 = 1U,
    PPC_MAILBOX_HOST_CMD_MEM_CRC32 = 2U
} ppc_mailbox_host_command;

/*
 * ABI v1 in emulated RAM.
 * All 32-bit fields are stored as big-endian values because PPC accesses
 * this memory in big-endian mode; host side must convert explicitly.
 *
 * Ordering contract:
 * Host writes cmd/args/payload first, then writes seq last.
 * PPC polls seq, processes request, writes status/results, then writes ack_seq last.
 */
typedef struct PPC_MAILBOX_PACKED ppc_mailbox_v1 {
    volatile uint32_t magic;
    volatile uint32_t abi_version;
    volatile uint32_t seq;
    volatile uint32_t ack_seq;
    volatile uint32_t cmd;
    volatile uint32_t status;
    volatile uint32_t arg0;
    volatile uint32_t arg1;
    volatile uint32_t result0;
    volatile uint32_t result1;
    uint8_t reserved[24];
    uint8_t payload[PPC_MAILBOX_PAYLOAD_BYTES];
    volatile uint32_t host_req_seq;
    volatile uint32_t host_ack_seq;
    volatile uint32_t host_cmd;
    volatile uint32_t host_status;
    volatile uint32_t host_arg0;
    volatile uint32_t host_arg1;
    volatile uint32_t host_result0;
    volatile uint32_t host_result1;
    uint8_t host_reserved[64];
} ppc_mailbox_v1;

#define PPC_MAILBOX_OFF_MAGIC ((uint32_t)offsetof(ppc_mailbox_v1, magic))
#define PPC_MAILBOX_OFF_ABI_VERSION ((uint32_t)offsetof(ppc_mailbox_v1, abi_version))
#define PPC_MAILBOX_OFF_SEQ ((uint32_t)offsetof(ppc_mailbox_v1, seq))
#define PPC_MAILBOX_OFF_ACK_SEQ ((uint32_t)offsetof(ppc_mailbox_v1, ack_seq))
#define PPC_MAILBOX_OFF_CMD ((uint32_t)offsetof(ppc_mailbox_v1, cmd))
#define PPC_MAILBOX_OFF_STATUS ((uint32_t)offsetof(ppc_mailbox_v1, status))
#define PPC_MAILBOX_OFF_ARG0 ((uint32_t)offsetof(ppc_mailbox_v1, arg0))
#define PPC_MAILBOX_OFF_ARG1 ((uint32_t)offsetof(ppc_mailbox_v1, arg1))
#define PPC_MAILBOX_OFF_RESULT0 ((uint32_t)offsetof(ppc_mailbox_v1, result0))
#define PPC_MAILBOX_OFF_RESULT1 ((uint32_t)offsetof(ppc_mailbox_v1, result1))
#define PPC_MAILBOX_OFF_PAYLOAD ((uint32_t)offsetof(ppc_mailbox_v1, payload))
#define PPC_MAILBOX_OFF_HOST_REQ_SEQ ((uint32_t)offsetof(ppc_mailbox_v1, host_req_seq))
#define PPC_MAILBOX_OFF_HOST_ACK_SEQ ((uint32_t)offsetof(ppc_mailbox_v1, host_ack_seq))
#define PPC_MAILBOX_OFF_HOST_CMD ((uint32_t)offsetof(ppc_mailbox_v1, host_cmd))
#define PPC_MAILBOX_OFF_HOST_STATUS ((uint32_t)offsetof(ppc_mailbox_v1, host_status))
#define PPC_MAILBOX_OFF_HOST_ARG0 ((uint32_t)offsetof(ppc_mailbox_v1, host_arg0))
#define PPC_MAILBOX_OFF_HOST_ARG1 ((uint32_t)offsetof(ppc_mailbox_v1, host_arg1))
#define PPC_MAILBOX_OFF_HOST_RESULT0 ((uint32_t)offsetof(ppc_mailbox_v1, host_result0))
#define PPC_MAILBOX_OFF_HOST_RESULT1 ((uint32_t)offsetof(ppc_mailbox_v1, host_result1))

_Static_assert(PPC_MAILBOX_OFF_PAYLOAD == 64U, "mailbox payload offset must remain 64");
_Static_assert(PPC_MAILBOX_OFF_HOST_REQ_SEQ == 576U, "host service lane offset changed");
_Static_assert(sizeof(ppc_mailbox_v1) <= PPC_MAILBOX_SIZE, "mailbox struct exceeds page size");

#endif
