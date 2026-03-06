// SPDX-License-Identifier: MIT

#include "m68k.h"
#include "emulator.h"
#include "platforms/platforms.h"
#include "input/input.h"
#include "m68kcpu.h"

#include "platforms/amiga/Gayle.h"
#include "platforms/amiga/amiga-registers.h"
#include "platforms/amiga/amiga-interrupts.h"
#include "platforms/amiga/pirtg64/pirtg64.h"
#include "platforms/amiga/hunk-reloc.h"
#include "platforms/amiga/piscsi/piscsi.h"
#include "platforms/amiga/piscsi/piscsi-enums.h"
#include "platforms/amiga/net/pi-net.h"
#include "platforms/amiga/net/pi-net-enums.h"
#include "platforms/amiga/ahi/pi_ahi.h"
#include "platforms/amiga/ahi/pi-ahi-enums.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"
#include "gpio/ps_protocol.h"
#include "log.h"
#include "memory_mapped.h"
#include "cpu_backend.h"
#ifdef USE_UAE_JIT
#ifdef __cplusplus
extern "C" {
#endif
#include "uae/pistorm_uae_bridge.h"
#ifdef __cplusplus
}
#endif
#endif

#include <assert.h>
#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>


#include "m68kops.h"
#include "emulator_fc.h"
#include "config_file/rominfo.h"

static void fc_callback_wrapper(unsigned int new_fc) {
  cpu_set_fc((uint32_t)new_fc);
}


#define KEY_POLL_INTERVAL_MSEC 1000

unsigned int ovl;

static uint32_t fc_shadow = 0;
static uint32_t fc_shadow_pc = 0;
static uint32_t fc_shadow_addr = 0;
static uint8_t fc_shadow_type = 0;
static uint8_t fc_shadow_is_write = 0;

static unsigned int fc_boot_log_remaining = 0;
static int fc_boot_log_inited = 0;

static int use_uae_jit = 0;

#if USE_UAE_JIT


static int lowvec_trace = -1;


static inline int lowvec_trace_enabled(void) {
  if (lowvec_trace == -1) {
    const char* e = getenv("PISTORM_UAE_LOWVEC_TRACE");
    lowvec_trace = (e && atoi(e) != 0) ? 1 : 0;
  }
  return lowvec_trace;
}
#endif /* USE_UAE_JIT */

static inline void lowvec_trace_log(const char* op, uint32_t addr, uint32_t val) {
#ifdef USE_UAE_JIT
  if (!use_uae_jit || !lowvec_trace_enabled()) {
    return;
  }
  if (!(addr < 0x00000200u || (addr >= 0x00F80000u && addr < 0x00F90000u))) {
    return;
  }
  printf("[UAE-LOWVEC] %s addr=%08X val=%08X pc=%08X regs_pc=%08X regs_pc_p=%08X ovl=%u\n",
         op,
         addr,
         val,
         uae_pistorm_get_pc(),
         uae_pistorm_get_regs_pc(),
         uae_pistorm_get_regs_pc_p(),
         ovl);
#else
  (void)op;
  (void)addr;
  (void)val;
#endif
}

static inline uint32_t cpu_backend_get_pc(void);

typedef struct mem_trace_cfg_s {
  int init_done;
  int enabled;
  int trace_reads;
  int trace_writes;
  int skip_fetch_reads;
  uint32_t base0;
  uint32_t size0;
  uint32_t base1;
  uint32_t size1;
  uint32_t limit;
  uint32_t emitted;
  int limit_noted;
} mem_trace_cfg_t;

static mem_trace_cfg_t g_mem_trace = {0};

static uint32_t mem_trace_parse_u32(const char* name, uint32_t default_value) {
  const char* env;
  char* endptr;
  unsigned long parsed;

  env = getenv(name);
  if ((env == NULL) || (env[0] == '\0')) {
    return default_value;
  }

  errno = 0;
  endptr = NULL;
  parsed = strtoul(env, &endptr, 0);
  if ((errno != 0) || (endptr == env) || ((endptr != NULL) && (*endptr != '\0'))) {
    return default_value;
  }
  if (parsed > 0xFFFFFFFFul) {
    return default_value;
  }
  return (uint32_t)parsed;
}

static int mem_trace_parse_bool(const char* name, int default_value) {
  const char* env;

  env = getenv(name);
  if ((env == NULL) || (env[0] == '\0')) {
    return default_value;
  }
  if ((strcmp(env, "1") == 0) || (strcasecmp(env, "true") == 0)
      || (strcasecmp(env, "yes") == 0) || (strcasecmp(env, "on") == 0)) {
    return 1;
  }
  if ((strcmp(env, "0") == 0) || (strcasecmp(env, "false") == 0)
      || (strcasecmp(env, "no") == 0) || (strcasecmp(env, "off") == 0)) {
    return 0;
  }
  return default_value;
}

static inline int mem_trace_range_match(uint32_t addr, uint32_t width, uint32_t base, uint32_t size) {
  uint64_t access_lo;
  uint64_t access_hi;
  uint64_t range_lo;
  uint64_t range_hi;

  if (size == 0u) {
    return 0;
  }

  access_lo = (uint64_t)addr;
  access_hi = access_lo + (uint64_t)width;
  range_lo = (uint64_t)base;
  range_hi = range_lo + (uint64_t)size;
  if ((access_lo < range_hi) && (access_hi > range_lo)) {
    return 1;
  }
  return 0;
}

static void mem_trace_init_once(void) {
  mem_trace_cfg_t* trace;

  trace = &g_mem_trace;
  if (trace->init_done != 0) {
    return;
  }

  trace->init_done = 1;
  trace->enabled = mem_trace_parse_bool("PISTORM_MEM_TRACE", 0);
  trace->trace_reads = mem_trace_parse_bool("PISTORM_MEM_TRACE_READS", 1);
  trace->trace_writes = mem_trace_parse_bool("PISTORM_MEM_TRACE_WRITES", 1);
  trace->skip_fetch_reads = mem_trace_parse_bool("PISTORM_MEM_TRACE_SKIP_FETCH", 1);
  trace->base0 = mem_trace_parse_u32("PISTORM_MEM_TRACE_BASE", 0x00EC0000u);
  trace->size0 = mem_trace_parse_u32("PISTORM_MEM_TRACE_SIZE", 0x00010000u);
  trace->base1 = mem_trace_parse_u32("PISTORM_MEM_TRACE_BASE2", 0u);
  trace->size1 = mem_trace_parse_u32("PISTORM_MEM_TRACE_SIZE2", 0u);
  trace->limit = mem_trace_parse_u32("PISTORM_MEM_TRACE_LIMIT", 2048u);
  trace->emitted = 0u;
  trace->limit_noted = 0;

  if (trace->enabled != 0) {
    LOG_INFO("[MEMTRACE] enabled r=%d w=%d skip_fetch=%d range0=$%08X+$%08X range1=$%08X+$%08X limit=%u\n",
             trace->trace_reads,
             trace->trace_writes,
             trace->skip_fetch_reads,
             trace->base0,
             trace->size0,
             trace->base1,
             trace->size1,
             trace->limit);
  }
}

static inline void mem_trace_log(const char* op,
                                 uint32_t addr,
                                 uint32_t value,
                                 uint32_t width,
                                 const char* source) {
  mem_trace_cfg_t* trace;
  uint32_t pc;
  int is_read;

  mem_trace_init_once();
  trace = &g_mem_trace;
  if (trace->enabled == 0) {
    return;
  }
  if ((mem_trace_range_match(addr, width, trace->base0, trace->size0) == 0)
      && (mem_trace_range_match(addr, width, trace->base1, trace->size1) == 0)) {
    return;
  }
  is_read = ((op != NULL) && (op[0] == 'R')) ? 1 : 0;
  if ((is_read != 0) && (trace->trace_reads == 0)) {
    return;
  }
  if ((is_read == 0) && (trace->trace_writes == 0)) {
    return;
  }
  if ((is_read != 0) && (trace->skip_fetch_reads != 0)) {
    pc = cpu_backend_get_pc();
    if (addr == pc) {
      return;
    }
  }
  if ((trace->limit != 0u) && (trace->emitted >= trace->limit)) {
    if (trace->limit_noted == 0) {
      trace->limit_noted = 1;
      LOG_INFO("[MEMTRACE] trace limit reached (%u events)\n", trace->limit);
    }
    return;
  }

  trace->emitted++;
  pc = cpu_backend_get_pc();
  LOG_INFO("[MEMTRACE] %s addr=$%08X val=$%08X src=%s pc=$%08X\n",
           op,
           addr,
           value,
           source,
           pc);
}


static const char* fc_mode_name(enum fc_mode mode) {
  switch (mode) {
  case FC_MODE_OFF:
    return "off";
  case FC_MODE_STUB:
    return "stub";
  case FC_MODE_CPLD:
    return "cpld";
  default:
    return "unknown";
  }
}

extern unsigned int cpu_type;

static int cpu_type_at_least_68020(unsigned int type) {
  switch (type) {
  case M68K_CPU_TYPE_68EC020:
  case M68K_CPU_TYPE_68020:
  case M68K_CPU_TYPE_68EC030:
  case M68K_CPU_TYPE_68030:
  case M68K_CPU_TYPE_68EC040:
  case M68K_CPU_TYPE_68LC040:
  case M68K_CPU_TYPE_68040:
  case M68K_CPU_TYPE_SCC68070:
    return 1;
  default:
    return 0;
  }
}

static const char* cpu_type_name(unsigned int type) {
  switch (type) {
  case M68K_CPU_TYPE_68000: return "68000";
  case M68K_CPU_TYPE_68010: return "68010";
  case M68K_CPU_TYPE_68EC020: return "68EC020";
  case M68K_CPU_TYPE_68020: return "68020";
  case M68K_CPU_TYPE_68EC030: return "68EC030";
  case M68K_CPU_TYPE_68030: return "68030";
  case M68K_CPU_TYPE_68EC040: return "68EC040";
  case M68K_CPU_TYPE_68LC040: return "68LC040";
  case M68K_CPU_TYPE_68040: return "68040";
  case M68K_CPU_TYPE_SCC68070: return "SCC68070";
  default: return "unknown";
  }
}

static void enforce_kickstart_cpu_compat(struct emulator_config* cfg_) {
  if (!cfg_) {
    return;
  }
  if (cpu_type_at_least_68020(cpu_type)) {
    return;
  }

  int kick_idx = -1;
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg_->map_type[i] != MAPTYPE_ROM || !cfg_->map_data[i] || !cfg_->map_id[i]) {
      continue;
    }
    if (strcasecmp(cfg_->map_id[i], "kickstart") == 0) {
      kick_idx = i;
      break;
    }
  }
  if (kick_idx < 0) {
    return;
  }

  struct romInfo info = {0};
  size_t rom_len = cfg_->rom_size[kick_idx] ? (size_t)cfg_->rom_size[kick_idx]
                                             : (size_t)cfg_->map_size[kick_idx];
  if (!queryRomInfo(cfg_->map_data[kick_idx], rom_len, &info)) {
    return;
  }
  if (!romInfoRequires68020(&info)) {
    return;
  }

  unsigned int old_cpu = cpu_type;
  cpu_type = M68K_CPU_TYPE_68020;
  cfg_->cpu_type = cpu_type;
  LOG_WARN("[CPU] Kickstart %u.%u (%s) requires at least 68020; overriding CPU %s -> 68020.\n",
           info.major, info.minor, cfg_->map_id[kick_idx],
           cpu_type_name(old_cpu));
  LOG_WARN("[CPU] Use Kickstart 3.1 r40.63 for 68000/68010-class A500 boot.\n");
}

static int read_kernel_param_bool(const char* name) {
  char path[128];
  char buf[8];
  int fd;
  ssize_t n;

  snprintf(path, sizeof(path), "/sys/module/pistorm/parameters/%s", name);
  fd = open(path, O_RDONLY);
  if (fd == -1) {
    return -1;
  }
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) {
    return -1;
  }
  buf[n] = '\0';
  return (buf[0] == '1' || buf[0] == 'Y' || buf[0] == 'y');
}

static inline uint32_t cpu_backend_get_pc(void) {
#ifdef USE_UAE_JIT
  if (use_uae_jit) {
    return uae_pistorm_get_pc();
  }
#endif
  return m68k_get_reg(NULL, M68K_REG_PC);
}

#if USE_UAE_JIT

static int uae_cpu_model_from_musashi(unsigned int type) {
  switch (type) {
  case M68K_CPU_TYPE_68000:
    return 68000;
  case M68K_CPU_TYPE_68010:
    return 68010;
  case M68K_CPU_TYPE_68EC020:
  case M68K_CPU_TYPE_68020:
    return 68020;
  case M68K_CPU_TYPE_68EC030:
  case M68K_CPU_TYPE_68030:
    return 68030;
  case M68K_CPU_TYPE_68EC040:
  case M68K_CPU_TYPE_68LC040:
  case M68K_CPU_TYPE_68040:
    return 68040;
  default:
    return 68030;
  }
}
#endif /* USE_UAE_JIT */


int kb_hook_enabled = 0;
int mouse_hook_enabled = 0;
int cpu_emulation_running = 1;
int swap_df0_with_dfx = 0;
int spoof_df0_id = 0;
int move_slow_to_chip = 0;
int force_move_slow_to_chip = 0;

uint8_t mouse_dx = 0;
uint8_t mouse_dy = 0;
uint8_t mouse_buttons = 0;
uint8_t mouse_buttons_latched = 0;
uint8_t mouse_extra = 0;

extern uint8_t gayle_int;
extern uint8_t gayle_ide_enabled;
extern uint8_t gayle_emulation_enabled;
extern uint8_t gayle_a4k_int;
extern volatile unsigned int* gpio;
extern volatile uint16_t srdata;
extern uint8_t realtime_graphics_debug;
extern uint8_t emulator_exiting;
extern uint8_t rtg_on;

uint8_t realtime_disassembly = 0;
uint8_t int2_enabled = 0;

uint32_t do_disasm = 0;
uint32_t old_level;
uint32_t last_irq = 8;
uint32_t last_last_irq = 8;

uint8_t ipl_enabled[8];

uint8_t end_signal = 0;
static volatile sig_atomic_t sigint_seen = 0;
static volatile sig_atomic_t crash_signal = 0;
static volatile uintptr_t crash_fault_addr = 0;
static volatile sig_atomic_t crash_si_code = 0;
static unsigned int last_pc_seen = 0;
uint8_t load_new_config = 0;
uint8_t enable_jit_backend = 0;
uint8_t enable_fpu_jit_backend = 0;
static int (*fpu_exec_hook)(m68ki_cpu_core* state, uint16_t opcode) = NULL;

static __thread char disasm_buf[4096];
// char disasm_buf[4096];

#define KICKBASE 0xF80000
#define KICKSIZE 0x7FFFF

int mem_fd = -1; 
int mouse_fd = -1;
int keyboard_fd = -1;
int mem_fd_gpclk;
int irq;
int gayleirq;

//#define CORE_AUTO -1  // ITS A GHOST...
#define CORE_MAIN  1   // keep main thread on core 1
#define CORE_CPU 3
#define CORE_IO 1
#define CORE_INPUT 2
#define CORE_IPL 2

#define PI_AFFINITY_ENV "PISTORM_AFFINITY" // e.g. "cpu=1, ipl=2, input=3, keyboard=3, mouse=3"
#define PI_RT_ENV "PISTORM_RT"             // e.g. "cpu=60, ipl=40, input=80, keyboard=90"

#define PISTORM64_NAME "KERNEL PiStorm64"
#define PISTORM64_TAGLINE "JANUS BUS ENGINE"

#define RT_DEFAULT_CPU 80
#define RT_DEFAULT_IO 60
#define RT_DEFAULT_INPUT 80
#define RT_DEFAULT_IPL 70

// Forward declarations for helpers used before their definitions.
static inline uint8_t opcode_is_fpu(uint16_t opcode);
static void apply_affinity_from_env(const char* role, int default_core);
static void set_realtime_priority(const char* name, int prio);
static void apply_realtime_from_env(const char* role, int default_prio);
static int realtime_allowed(void);

static void amiga_reset_and_wait(const char* tag);
static void amiga_warmup_bus(void);

static void configure_ipl_nops(void);
static void print_help(const char* prog);
static void print_about(const char* prog);

extern unsigned int cpu_type;
extern struct emulator_config* cfg;

static void dump_cpu_state(const char *reason, int opcode) {
  unsigned int pc = m68k_get_reg(NULL, M68K_REG_PC);
  unsigned int ppc = m68k_get_reg(NULL, M68K_REG_PPC);
  unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
  if (reason) {
    LOG_ERROR("[CPU] %s: PC=$%.8X PPC=$%.8X SR=$%.4X OPCODE=$%.4X\n",
              reason, pc, ppc, sr, (unsigned int)(opcode & 0xFFFF));
  } else {
    LOG_ERROR("[CPU] PC=$%.8X PPC=$%.8X SR=$%.4X\n", pc, ppc, sr);
  }
  if (crash_signal) {
    LOG_ERROR("[CPU] Signal detail: signo=%d si_code=%d fault_addr=%p\n",
              (int)crash_signal, (int)crash_si_code, (void*)crash_fault_addr);
    if (cfg && crash_fault_addr) {
      uintptr_t fa = (uintptr_t)crash_fault_addr;
      int host_map_found = 0;
      for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
        if (cfg->map_type[i] == MAPTYPE_NONE || !cfg->map_data[i]) {
          continue;
        }
        uintptr_t base = (uintptr_t)cfg->map_data[i];
        uintptr_t span = (uintptr_t)cfg->map_size[i];
        if (cfg->map_type[i] == MAPTYPE_ROM || cfg->map_type[i] == MAPTYPE_RAM_WTC) {
          if ((uintptr_t)cfg->rom_size[i] > span) {
            span = (uintptr_t)cfg->rom_size[i];
          }
        }
        if (!span) {
          continue;
        }
        if (fa >= base && fa < (base + span)) {
          uint32_t off = (uint32_t)(fa - base);
          uint32_t guest = (uint32_t)cfg->map_offset[i] + off;
          LOG_ERROR("[CPU] Fault host map[%d]: type=%u id=%s host_base=%p off=0x%X guest=$%.8X\n",
                    i, (unsigned int)cfg->map_type[i],
                    cfg->map_id[i] ? cfg->map_id[i] : "None",
                    (void*)base, off, guest);
          host_map_found = 1;
          break;
        }
      }
      if (!host_map_found) {
        LOG_ERROR("[CPU] Fault host addr not in cfg map_data ranges\n");
      }
    }
  }

  m68k_disassemble(disasm_buf, pc, cpu_type);
  LOG_ERROR("[CPU] %s\n", disasm_buf);
  LOG_ERROR("REGA: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n",
            m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
            m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3),
            m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5),
            m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7));
  LOG_ERROR("REGD: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n",
            m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
            m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
            m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5),
            m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));
  if (last_pc_seen != 0 && last_pc_seen != pc) {
    m68k_disassemble(disasm_buf, last_pc_seen, cpu_type);
    LOG_ERROR("[CPU] last_pc=$%.8X %s\n", last_pc_seen, disasm_buf);
  }

  if (cfg) {
    mem_map_entry_info_t map_info;
    if (memmap_lookup(cfg, pc, &map_info) >= 0) {
      uint32_t amiga_end = map_info.amiga_end_exclusive ? (map_info.amiga_end_exclusive - 1u)
                                                        : map_info.amiga_end_exclusive;
      LOG_ERROR("[CPU] PC map[%d] amiga=$%.8X-$%.8X size=$%.8X host=%p host_span=$%.8X "
                "type=%u kind=%s cacheable=%u executable=%u id=%s\n",
                map_info.index, map_info.amiga_base, amiga_end, map_info.size,
                map_info.host_ptr, map_info.host_span,
                (unsigned int)map_info.map_type, memmap_kind_name(map_info.kind),
                (unsigned int)map_info.cacheable, (unsigned int)map_info.executable, map_info.map_id);
      if (map_info.map_type == MAPTYPE_ROM && map_info.host_ptr) {
        uint32_t off = pc - map_info.amiga_base;
        unsigned char *base = (unsigned char*)map_info.host_ptr;
        char line[128];
        int pos = snprintf(line, sizeof(line), "[CPU] ROM bytes:");
        for (int i = -8; i < 10; i++) {
          uint32_t idx = off + (uint32_t)i;
          unsigned char b = base[idx % (uint32_t)cfg->rom_size[map_info.index]];
          pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " %.2X", b);
        }
        LOG_ERROR("%s\n", line);
      }
    } else {
      LOG_ERROR("[CPU] PC map: unmapped\n");
    }
  }
}

static void instr_hook_callback(unsigned int pc) {
  last_pc_seen = pc;
}

static int illg_instr_callback(int opcode) {
  dump_cpu_state("Illegal instruction", opcode);
  return 0; // let Musashi raise the exception normally
}


#if USE_UAE_JIT
static void crash_signal_handler_siginfo(int sig_num, siginfo_t* info, void* uctx) {
  (void)uctx;
  crash_signal = sig_num;
  crash_fault_addr = info ? (uintptr_t)info->si_addr : 0;
  crash_si_code = info ? info->si_code : 0;
  dump_cpu_state("Signal", -1);
  _exit(128 + sig_num);
}
#endif


static void fc_boot_log_init(void)
{
  if (fc_boot_log_inited) {
    return;
  }
  fc_boot_log_inited = 1;

  const char *env = getenv("PISTORM_FC_BOOT_LOG");
  if (env && *env) {
    fc_boot_log_remaining = (unsigned int)strtoul(env, NULL, 0);
  } else {
    // Default silent; set PISTORM_FC_BOOT_LOG to enable.
    fc_boot_log_remaining = 0;
  }
}

static inline const char *fc_space_name(uint8_t fc)
{
  switch (fc & 0x7u) {
  case 0: return "reserved";
  case 1: return "user-data";
  case 2: return "user-prog";
  case 3: return "reserved";
  case 4: return "reserved";
  case 5: return "super-data";
  case 6: return "super-prog";
  case 7: return "cpu-space";
  default: return "reserved";
  }
}

static inline void fc_shadow_touch(uint8_t type, uint32_t addr, uint8_t is_write)
{
  if (fc_get_mode() == FC_MODE_OFF) {
    return;
  }

  fc_boot_log_init();
  if (fc_boot_log_remaining == 0) {
    // Logging disabled or limit reached
    return;
  }

  uint8_t fc_val = (uint8_t)(current_fc & 0x7u);

  // Do not spam identical repeats of {FC, addr, type, RW}
  if (fc_val == fc_shadow &&
      addr   == fc_shadow_addr &&
      type   == fc_shadow_type &&
      is_write == fc_shadow_is_write) {
    return;
  }

  fc_shadow        = fc_val;
  fc_shadow_pc     = cpu_backend_get_pc();
  fc_shadow_addr   = addr;
  fc_shadow_type   = type;
  fc_shadow_is_write = is_write;

  const char *space = fc_space_name(fc_val);

  LOG_INFO("[FC] seen=%u (%s) %s type=%u addr=$%.8X PC=$%.8X\n",
           fc_shadow,
           space,
           is_write ? "W" : "R",
           type,
           addr,
           fc_shadow_pc);

  fc_boot_log_remaining--;
}



#define CLI_MAX_LINES 32
static char* cli_config_lines[CLI_MAX_LINES];
static int cli_config_count;

static void cli_add_line(const char* fmt, ...);
static void apply_cli_overrides(struct emulator_config* cfg);
static int cli_collect_tokens(int argc, char* argv[], int* index, char* out, size_t out_len);

#define MUSASHI_HAX

#ifdef MUSASHI_HAX
#include "m68kcpu.h"
extern m68ki_cpu_core m68ki_cpu;

#define M68K_SET_IRQ(i) m68k_set_irq_state(&m68ki_cpu, (i))
#define M68K_END_TIMESLICE m68k_end_timeslice_state(&m68ki_cpu)
#else
#define M68K_SET_IRQ m68k_set_irq
#define M68K_END_TIMESLICE m68k_end_timeslice()
#endif

#define NOP1()  __asm__ __volatile__("nop" ::: "memory")
#define NOP 	do { NOP1(); NOP1(); NOP1(); NOP1(); } while (0)

#define DEBUG_EMULATOR

#ifdef DEBUG_EMULATOR
#define DEBUG LOG_DEBUG
#else
#define DEBUG(...)
#endif

// Configurable emulator options
unsigned int cpu_type = M68K_CPU_TYPE_68000;
unsigned int loop_cycles = 1024;
static unsigned int ipl_nop_count = 8;
static const unsigned int ipl_nop_count_default = 8;
unsigned int irq_status = 0;

static const unsigned int loop_cycles_cap = 2097152; // cap slices to keep service latency reasonable
struct emulator_config* cfg = NULL;
char keyboard_file[256] = "/dev/input/event1";

uint64_t trig_irq = 0, serv_irq = 0;
uint16_t irq_delay = 0;
unsigned int amiga_reset = 0;
unsigned int amiga_reset_last = 0;
unsigned int do_reset = 0;


static void amiga_warmup_bus(void) {
  for (int i = 0; i < 64; i++) {
    (void)ps_read_status_reg();
    if ((i & 0x0f) == 0) {
      usleep(100);
    }
  }
}



static void amiga_reset_and_wait(const char* tag) {
  for (int attempt = 0; attempt < 3; attempt++) {
    ps_reset_state_machine();
    ps_pulse_reset();
    usleep(1500);

    int timeout_us = 20000;
    while (timeout_us > 0) {
      if (!(ps_gpio_lev() & (1 << PIN_TXN_IN_PROGRESS))) {
        amiga_warmup_bus();
        return;
      }
      usleep(10);
      timeout_us -= 10;
    }
    usleep(2000);
  }
  printf("[RST] Warning: TXN_IN_PROGRESS still set after reset (%s)\n", tag);
}


static void configure_ipl_nops(void) {
  unsigned int value = ipl_nop_count_default;
  const char* env = getenv("PISTORM_IPL_NOP_COUNT");
  if (env && *env) {
    unsigned long parsed = strtoul(env, NULL, 10);
    if (parsed > 4096ul) parsed = 4096ul;
    value = (unsigned int)parsed;
  } else if (loop_cycles > 2097152) {
    unsigned long scaled = ((unsigned long)ipl_nop_count_default * (unsigned long)loop_cycles) / 300ul;
    if (scaled < ipl_nop_count_default) scaled = ipl_nop_count_default;
    if (scaled > 4096ul) scaled = 4096ul;
    value = (unsigned int)scaled;
  }
  ipl_nop_count = value;
  printf("[CFG] IPL NOP count: %u\n", ipl_nop_count);
}

// Compile-time toggle for IPL rate limiting - default to disabled to ensure stability
#ifndef PISTORM_IPL_RATELIMIT_US
#define PISTORM_IPL_RATELIMIT_US 0
#endif

#if PISTORM_IPL_RATELIMIT_US > 0
// Helper function for rate limiting
static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#endif

static void* ipl_task(void* args) {
  printf("[IPL] Thread running\n");
  uint16_t old_irq = 0;
  uint32_t value;

#if PISTORM_IPL_RATELIMIT_US > 0
  // Rate limiting variables for GPIO/status polling
  static uint64_t last_ns = 0;
  static const uint64_t poll_interval_ns = (uint64_t)PISTORM_IPL_RATELIMIT_US * 1000ull; // Convert us to ns
#endif

  while (1) {
    if (emulator_exiting || end_signal) {
      break;
    }

#if PISTORM_IPL_RATELIMIT_US > 0
    // Check if enough time has passed since last poll
    uint64_t t = now_ns();
    if (t - last_ns >= poll_interval_ns) {
        value = ps_gpio_lev();
        last_ns = t;
    } else {
        // Use cached value if not enough time has passed
        continue;
    }
#else
    // Original behavior - always poll
    value = ps_gpio_lev();
#endif

    if (value & (1 << PIN_TXN_IN_PROGRESS)) {
      goto noppers;
    }

#if USE_UAE_JIT
    if (use_uae_jit) {
      if (!(value & (1 << PIN_IPL_ZERO)) || ipl_enabled[amiga_emulated_ipl()]) {
        if (!irq) {
          irq = 1;
        }
        last_irq = (uint32_t)((ps_read_status_reg() & 0xe000) >> 13);
        uint8_t amiga_irq = amiga_emulated_ipl();
        if (amiga_irq >= last_irq) {
          last_irq = amiga_irq;
        }
        if (last_irq != 0 && last_irq != last_last_irq) {
          last_last_irq = last_irq;
#ifdef USE_UAE_JIT
          if (use_uae_jit) {
            uae_pistorm_set_irq((int)last_irq);
          } else {
            M68K_SET_IRQ((int)last_irq);
          }
#else
          M68K_SET_IRQ((int)last_irq);
#endif
        }
      } else {
        if (irq) {
          irq = 0;
        }
        if (last_last_irq != 0) {
#ifdef USE_UAE_JIT
          if (use_uae_jit) {
            uae_pistorm_set_irq(0);
          } else {
            M68K_SET_IRQ(0);
          }
#else
          M68K_SET_IRQ(0);
#endif
          last_last_irq = 0;
        }
      }

      if (do_reset == 0) {
        amiga_reset = (value & (1 << PIN_RESET));
        if (amiga_reset != amiga_reset_last) {
          amiga_reset_last = amiga_reset;
          if (amiga_reset == 0) {
            printf("Amiga Reset is down...\n");
            do_reset = 1;
          } else {
            printf("Amiga Reset is up...\n");
          }
        }
      }

      if (do_reset) {
#ifdef USE_UAE_JIT
        if (use_uae_jit) {
          uae_pistorm_pulse_reset();
        } else {
          m68k_pulse_reset(NULL);
        }
#else
        m68k_pulse_reset(NULL);
#endif
        do_reset = 0;
        rtg_on = 0;
      }

      ps_flush_batch_queue();
      goto noppers;
    }
#endif /* USE_UAE_JIT */

    if (!(value & (1 << PIN_IPL_ZERO)) || ipl_enabled[amiga_emulated_ipl()]) {
      old_irq = irq_delay;
      // NOP
      if (!irq) {
        M68K_END_TIMESLICE;
        NOP;
        irq = 1;
      }
      // usleep( 0 );
    } else {
      if (irq) {
        if (old_irq) {
          old_irq--;
        } else {
          irq = 0;
        }
        M68K_END_TIMESLICE;
        NOP;
        // usleep( 0 );
      }
    }
    if (do_reset == 0) {
      amiga_reset = (value & (1 << PIN_RESET));
      if (amiga_reset != amiga_reset_last) {
        amiga_reset_last = amiga_reset;
        if (amiga_reset == 0) {
          printf("Amiga Reset is down...\n");
          do_reset = 1;
          M68K_END_TIMESLICE;
        } else {
          printf("Amiga Reset is up...\n");
        }
      }
    }

    /*if ( gayle_ide_enabled ) {
      if ( ( ( gayle_int & 0x80 ) || gayle_a4k_int ) && ( get_ide( 0 )->drive[0].intrq || get_ide( 0
    )->drive[1].intrq ) ) {
        //get_ide( 0 )->drive[0].intrq = 0;
        gayleirq = 1;
        M68K_END_TIMESLICE;
      }
      else
        gayleirq = 0;
    }*/
    // usleep( 0 );
    // NOP NOP
  noppers:
    /*
      Deterministic, low-jitter pacing for the IPL/status polling path.
      This prevents hammering TXN_IN_PROGRESS, gives the CPLD state machine
      time to advance, and avoids scheduler noise vs usleep(). It also reduces
      contention with the main emulation loop. Removing or reducing this can
      destabilize polling and steal time from the main emulation loop.
    */
    for (unsigned int i = 0; i < ipl_nop_count; i++) {
      NOP;
    }
  }
  printf("[IPL] Thread exiting\n");
  return args;
}

static inline void m68k_execute_bef(m68ki_cpu_core* state, int num_cycles) {
  /* eat up any reset cycles */
  if (RESET_CYCLES) {
    int rc = (int)RESET_CYCLES;
    RESET_CYCLES = 0;
    num_cycles -= rc;
    if (num_cycles <= 0)
      return;
  }

  /* Set our pool of clock cycles available */
  SET_CYCLES(num_cycles);
  CPU_INITIAL_CYCLES = num_cycles;

  /* See if interrupts came in */
  m68ki_check_interrupts(state);

  /* Make sure we're not stopped */
  if (!CPU_STOPPED) {
    /* Return point if we had an address error */

#if M68K_EMULATE_ADDRESS_ERROR
    /* Return point if we had an address error */
    m68ki_set_address_error_trap(state); /* auto-disable ( see m68kcpu.h ) */
#endif


#ifdef M68K_BUSERR_THING
    m68ki_check_bus_error_trap(state);
#endif

    /* Main loop.  Keep going until we run out of clock cycles */
    do {
      /* Set tracing according to T1. ( T0 is done inside instruction ) */
      m68ki_trace_t1(); /* auto-disable ( see m68kcpu.h ) */

      /* Set the address space for reads */
      m68ki_use_data_space(); /* auto-disable ( see m68kcpu.h ) */

      /* Call external hook to peek at CPU */
      m68ki_instr_hook(state, REG_PC); /* auto-disable ( see m68kcpu.h ) */

      /* Record previous program counter */
      REG_PPC = REG_PC;

      /* Record previous D/A register state ( in case of bus error ) */
//#define M68K_BUSERR_THING
#ifdef M68K_BUSERR_THING
      for (int i = 15; i >= 0; i--) {
        REG_DA_SAVE[i] = REG_DA[i];
      }
#endif

      /* Read an instruction and call its handler */
      REG_IR = (uint16_t)m68ki_read_imm_16(state);
      if (fpu_exec_hook && opcode_is_fpu((uint16_t)REG_IR)) {
        fpu_exec_hook(state, (uint16_t)REG_IR);
      } else {
        m68ki_instruction_jump_table[REG_IR](state);
      }
      USE_CYCLES(CYC_INSTRUCTION[REG_IR]);

      /* Trace m68k_exception, if necessary */
      m68ki_exception_if_trace(state); /* auto-disable ( see m68kcpu.h ) */
    } while (GET_CYCLES() > 0);

    /* set previous PC to current PC for the next entry into the loop */
    REG_PPC = REG_PC;
  } else {
    SET_CYCLES(0);
  }

  /* return how many clocks we used */
  //return;
}

// Backend wrappers ( Musashi default, JIT stub delegates to Musashi for now ).
void musashi_backend_execute(m68ki_cpu_core* state, int cycles) {
  m68k_execute_bef(state, cycles);
}

void musashi_backend_set_irq(int level) {
  M68K_SET_IRQ(level);
}

// FPU backend stub: routes F-line opcodes through JIT path when enabled.
static inline uint8_t opcode_is_fpu(uint16_t opcode) {
  return ((opcode & 0xF000) == 0xF000);
}

// Tiny fast-path placeholder: try a host-side translation first, otherwise fall back.
static inline int fpu_translate_fastpath(m68ki_cpu_core* state, uint16_t opcode) {
  // TODO: implement host-side float ops translation. Currently always fall back.
  (void)state;
  (void)opcode;
  return 0;
}

static inline int fpu_backend_execute(m68ki_cpu_core* state, uint16_t opcode) {
  if (fpu_translate_fastpath(state, opcode)) {
    return 1;
  }
  m68ki_instruction_jump_table[opcode](state);
  return 1;
}

void jit_backend_execute(m68ki_cpu_core* state, int cycles) {
  musashi_backend_execute(state, cycles);
}

void jit_backend_set_irq(int level) {
  musashi_backend_set_irq(level);
}

static inline void cpu_backend_execute(m68ki_cpu_core* state, int cycles) {
  if (enable_jit_backend) {
    jit_backend_execute(state, cycles);
  } else {
    musashi_backend_execute(state, cycles);
  }
}

static inline void cpu_backend_set_irq(int level) {
  if (enable_jit_backend) {
    jit_backend_set_irq(level);
  } else {
    musashi_backend_set_irq(level);
  }
}

static void* cpu_task(void *arg) {
  (void)arg;
  m68ki_cpu_core* state = &m68ki_cpu;
  state->ovl = ovl;
#ifdef PISTORM_KMOD
  state->gpio = gpio;
#else
  state->gpio = NULL; // When kernel module is disabled, use NULL for gpio
#endif
  
#if USE_UAE_JIT
  if (!use_uae_jit) {
    m68k_pulse_reset(state);
  }
#endif /* USE_UAE_JIT */
  apply_affinity_from_env("cpu", CORE_CPU);
  apply_realtime_from_env("cpu", RT_DEFAULT_CPU);

cpu_loop:
#ifdef USE_UAE_JIT
  if (use_uae_jit) {
    while (!end_signal && !emulator_exiting) {
      uae_pistorm_run();
    }
    goto stop_cpu_emulation;
  }
#endif
  if (realtime_disassembly && (do_disasm || cpu_emulation_running)) {
    m68k_disassemble(disasm_buf, m68k_get_reg(NULL, M68K_REG_PC), cpu_type);
    printf("REGA: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n",
           m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
           m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3),
           m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5),
           m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7));
    printf("REGD: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n",
           m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
           m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
           m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5),
           m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));
    printf("%.8X ( %.8X )]] %s\n", m68k_get_reg(NULL, M68K_REG_PC),
           (m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF), disasm_buf);
    if (do_disasm) {
      do_disasm--;
    }
    cpu_backend_execute(state, 1);
    // Check for end_signal immediately after CPU execution to be more responsive
    if (end_signal) {
      goto stop_cpu_emulation;
    }
  } else {
    if (cpu_emulation_running) {
      unsigned int slice = loop_cycles > loop_cycles_cap ? loop_cycles_cap : loop_cycles;
      if (irq) {
        cpu_backend_execute(state, 5);
      } else {
        cpu_backend_execute(state, (int)slice);
      }
    }
  }

  // Check for end_signal immediately after CPU execution to be more responsive
  if (end_signal) {
    goto stop_cpu_emulation;
  }

  // Flush any pending batched operations before checking status
  ps_flush_batch_queue();

  if (irq) {
    last_irq = (uint32_t)((ps_read_status_reg() & 0xe000) >> 13);
    uint8_t amiga_irq = amiga_emulated_ipl();
    if (amiga_irq >= last_irq) {
      last_irq = amiga_irq;
    }
    if (last_irq != 0 && last_irq != last_last_irq) {
      last_last_irq = last_irq;
      cpu_backend_set_irq((int)last_irq);
    }
  }

  if (!irq && last_last_irq != 0) {
    cpu_backend_set_irq(0);
    last_last_irq = 0;
  }

  if (do_reset) {
    cpu_pulse_reset();
    do_reset = 0;
    usleep(1000000); // 1sec
    rtg_on = 0;
    //    while( amiga_reset==0 );
    //    printf( "CPU emulation reset.\n" );
  }

  // Flush any pending batched operations at the end of each CPU loop iteration
  ps_flush_batch_queue();

  if (mouse_hook_enabled && (mouse_extra != 0x00)) {
    // mouse wheel events have occurred; unlike l/m/r buttons, these are queued as keypresses, so
    // add to end of buffer
    switch (mouse_extra) {
    case 0xff:
      // wheel up
      queue_keypress(0xfe, KEYPRESS_PRESS, PLATFORM_AMIGA);
      break;
    case 0x01:
      // wheel down
      queue_keypress(0xff, KEYPRESS_PRESS, PLATFORM_AMIGA);
      break;
    }

    // dampen the scroll wheel until next while loop iteration
    mouse_extra = 0x00;
  }

  if (load_new_config) {
    printf("[CPU] Loading new config file.\n");
    goto stop_cpu_emulation;
  }

  if (end_signal) {
    goto stop_cpu_emulation;
  }

  goto cpu_loop;

stop_cpu_emulation:
  printf("[CPU] End of CPU thread\n");
  return (void*)NULL;
}

static void* keyboard_task(void *arg) {
  (void)arg;
  struct pollfd kbdpoll[1];
  int kpollrc;
  char c = 0, c_code = 0, c_type = 0;
  char grab_message[] = "[KBD] Grabbing keyboard from input layer",
       ungrab_message[] = "[KBD] Ungrabbing keyboard";
  static int keycode_samples_left = 5;

  printf("[KBD] Keyboard thread started\n");
  apply_affinity_from_env("keyboard", CORE_INPUT);
  apply_realtime_from_env("keyboard", RT_DEFAULT_INPUT);

  // because we permit the keyboard to be grabbed on startup, quickly check if we need to grab it
  if (kb_hook_enabled && cfg->keyboard_grab) {
    puts(grab_message);
    grab_device(keyboard_fd);
  }

  kbdpoll[0].fd = keyboard_fd;
  kbdpoll[0].events = POLLIN;

key_loop:
  if (emulator_exiting || end_signal) {
    goto key_end;
  }
  kpollrc = poll(kbdpoll, 1, KEY_POLL_INTERVAL_MSEC);
  if ((kpollrc > 0) && (kbdpoll[0].revents & POLLHUP)) {
    // in the event that a keyboard is unplugged, keyboard_task will whiz up to 100% utilisation
    // this is undesired, so if the keyboard HUPs, end the thread without ending the emulation
    printf("[KBD] Keyboard node returned HUP ( unplugged? )\n");
    goto key_end;
  }

  // if kpollrc > 0 then it contains number of events to pull, also check if POLLIN is set in
  // revents
  if ((kpollrc <= 0) || !(kbdpoll[0].revents & POLLIN)) {
    if (cfg->platform->id == PLATFORM_AMIGA && last_irq != 2 && get_num_kb_queued()) {
      amiga_emulate_irq(PORTS);
    }
    goto key_loop;
  }

  while (get_key_char(&c, &c_code, &c_type)) {
    if (keycode_samples_left > 0 && c_type == KEYPRESS_PRESS) {
      printf("[KBD] sample keycode=%u (0x%02X) type=%u char=%d\n",
             (uint8_t)c_code, (uint8_t)c_code, (uint8_t)c_type, (int)(unsigned char)c);
      keycode_samples_left--;
    }
    if (c && c == cfg->keyboard_toggle_key && !kb_hook_enabled) {
      kb_hook_enabled = 1;
      printf("[KBD] Keyboard hook enabled.\n");
      if (cfg->keyboard_grab) {
        grab_device(keyboard_fd);
        puts(grab_message);
      }
    } else if (kb_hook_enabled) {
      if (c == 0x1B && c_type) {
        kb_hook_enabled = 0;
        printf("[KBD] Keyboard hook disabled.\n");
        if (cfg->keyboard_grab) {
          release_device(keyboard_fd);
          puts(ungrab_message);
        }
      } else {
        if (queue_keypress(c_code, c_type, cfg->platform->id)) {
          if (cfg->platform->id == PLATFORM_AMIGA && last_irq != 2) {
            amiga_emulate_irq(PORTS);
          }
        }
      }
    }

    // pause pressed; trigger nmi ( int level 7 )
    if (c == 0x01 && c_type) {
      printf("[INT] Sending NMI\n");
      M68K_SET_IRQ(7);
    }

    if (!kb_hook_enabled && c_type) {
      if (c && c == cfg->mouse_toggle_key) {
        mouse_hook_enabled ^= 1;
        printf("Mouse hook %s.\n", mouse_hook_enabled ? "enabled" : "disabled");
        mouse_dx = mouse_dy = mouse_buttons = mouse_buttons_latched = mouse_extra = 0;
      }
      if (c == 'r') {
        cpu_emulation_running ^= 1;
        printf("CPU emulation is now %s\n", cpu_emulation_running ? "running" : "stopped");
      }
      if (c == 'g') {
        realtime_graphics_debug ^= 1;
        printf("Real time graphics debug is now %s\n", realtime_graphics_debug ? "on" : "off");
      }
      if (c == 'R') {
        cpu_pulse_reset();
        // m68k_pulse_reset(  );
        printf("CPU emulation reset.\n");
      }
      if (c == 'q') {
        printf("Quitting and exiting emulator.\n");
        end_signal = 1;
        goto key_end;
      }
      if (c == 'd') {
        realtime_disassembly ^= 1;
        do_disasm = 1;
        printf("Real time disassembly is now %s\n", realtime_disassembly ? "on" : "off");
      }
      if (c == 'D') {
        int r = get_mapped_item_by_address(cfg, 0x08000000);
        if (r != -1) {
          printf("Dumping first 16MB of mapped range %d.\n", r);
          FILE* dmp = fopen("./memdmp.bin", "wb+");
          fwrite(cfg->map_data[r], 16 * SIZE_MEGA, 1, dmp);
          fclose(dmp);
        }
      }
      if (c == 's' && realtime_disassembly) {
        do_disasm = 1;
      }
      if (c == 'S' && realtime_disassembly) {
        do_disasm = 128;
      }
    }
  }

  goto key_loop;

key_end:
  printf("[KBD] Keyboard thread ending\n");
  if (cfg->keyboard_grab) {
    puts(ungrab_message);
    release_device(keyboard_fd);
  }
  return (void*)NULL;
}

static void* mouse_task(void *arg) {
  (void)arg;
  struct pollfd mpoll[1];
  int mpollrc;

  printf("[MOUSE] Mouse thread started\n");
  apply_affinity_from_env("mouse", CORE_INPUT);
  apply_realtime_from_env("mouse", RT_DEFAULT_INPUT);

  mpoll[0].fd = mouse_fd;
  mpoll[0].events = POLLIN;

mouse_loop:
  mpollrc = poll(mpoll, 1, 10);
  if (mpollrc < 0 && errno == EINTR) {
    goto mouse_loop;
  }

  if (mpollrc > 0 && (mpoll[0].revents & POLLIN)) {
    uint8_t x, y, b, e;
    while (get_mouse_status(&x, &y, &b, &e)) {
      mouse_buttons = b;
      mouse_buttons_latched |= (uint8_t)(b & 0x07u);
      mouse_extra = e;
      mouse_dx = x;
      mouse_dy = y;
    }
  }

  if (!emulator_exiting && !end_signal) {
    goto mouse_loop;
  }

  printf("[MOUSE] Mouse thread exiting\n");
  return (void*)NULL;
}

void stop_cpu_emulation(uint8_t disasm_cur) {
  M68K_END_TIMESLICE;
  if (disasm_cur) {
    m68k_disassemble(disasm_buf, m68k_get_reg(NULL, M68K_REG_PC), cpu_type);
    printf("REGA: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n",
           m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1),
           m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3),
           m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5),
           m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7));
    printf("REGD: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n",
           m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
           m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
           m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5),
           m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));
    printf("%.8X ( %.8X )]] %s\n", m68k_get_reg(NULL, M68K_REG_PC),
           (m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF), disasm_buf);
    realtime_disassembly = 1;
  }

  cpu_emulation_running = 0;
  do_disasm = 0;
}

static void sigint_handler(int sig_num) {
  (void)sig_num;
  sigint_seen = 1;
  end_signal = 1;
  emulator_exiting = 1;
}

int main(int argc, char* argv[]) {
  apply_affinity_from_env("main", CORE_MAIN);
  int g;

  for (g = 1; g < argc; g++) {
    if (strcmp(argv[g], "-h") == 0 || strcmp(argv[g], "--help") == 0) {
      print_help(argv[0]);
      return 0;
    }
    if (strcmp(argv[g], "-a") == 0 || strcmp(argv[g], "--about") == 0) {
      print_about(argv[0]);
      return 0;
    }
  }

  pistorm_selftest_alignment();

  ps_setup_protocol();

  log_set_level(LOG_LEVEL_INFO);
  const char* syslog_env = getenv("PISTORM_SYSLOG");
  if (syslog_env && syslog_env[0] != '\0' &&
      strcasecmp(syslog_env, "0") != 0 &&
      strcasecmp(syslog_env, "false") != 0 &&
      strcasecmp(syslog_env, "no") != 0) {
    log_set_syslog(1);
  }

  // const struct sched_param priority = {99};

  // Some command line switch stuffles
  for (g = 1; g < argc; g++) {
    if (strcmp(argv[g], "--log") == 0) {
      const char* path = "amiga.log";
      if (g + 1 < argc && argv[g + 1][0] != '-') {
        g++;
        path = argv[g];
      }
      if (log_set_file(path) != 0) {
        printf("Failed to open log file %s.\n", path);
      }
    } else if (strcmp(argv[g], "--syslog") == 0) {
      log_set_syslog(1);
    } else if (strcmp(argv[g], "--affinity") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no affinity spec provided.\n", argv[g]);
      } else {
        cli_add_line("affinity %s", argv[++g]);
      }
    } else if (strcmp(argv[g], "--rtprio") == 0 || strcmp(argv[g], "--rt-prio") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no RT priority spec provided.\n", argv[g]);
      } else {
        cli_add_line("rtprio %s", argv[++g]);
      }
    } else if (strcmp(argv[g], "--log-level") == 0 || strcmp(argv[g], "--debug-level") == 0 ||
               strcmp(argv[g], "-l") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no log level specified.\n", argv[g]);
      } else {
        int level = log_parse_level(argv[++g]);
        if (level < 0) {
          printf("Invalid log level %s ( use error|warn|info|debug|verbose ).\n", argv[g]);
        } else {
          log_set_level(level);
        }
      }
    }
    if (strcmp(argv[g], "--cpu_type") == 0 || strcmp(argv[g], "--cpu") == 0 ||
        strcmp(argv[g], "-C") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no CPU type specified.\n", argv[g]);
      } else {
        g++;
        cli_add_line("cpu %s", argv[g]);
      }
    } else if (strcmp(argv[g], "--config-file") == 0 || strcmp(argv[g], "--config") == 0 ||
               strcmp(argv[g], "-c") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no config filename specified.\n", argv[g]);
      } else {
        g++;
        FILE* chk = fopen(argv[g], "rb");
        if (chk == NULL) {
          printf("Config file %s does not exist, please check that you've specified the path "
                 "correctly.\n",
                 argv[g]);
        } else {
          fclose(chk);
          load_new_config = 1;
          set_pistorm_devcfg_filename(argv[g]);
        }
      }
    } else if (strcmp(argv[g], "--enable-jit") == 0 || strcmp(argv[g], "--jit") == 0 ||
               strcmp(argv[g], "-j") == 0) {
      cli_add_line("jit on");
    } else if (strcmp(argv[g], "--enable-jit-fpu") == 0 || strcmp(argv[g], "--jit-fpu") == 0 ||
               strcmp(argv[g], "-f") == 0) {
      cli_add_line("jitfpu on");
    } else if (strcmp(argv[g], "--loopcycles") == 0 || strcmp(argv[g], "-L") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no loopcycles value specified.\n", argv[g]);
      } else {
        g++;
        cli_add_line("loopcycles %s", argv[g]);
      }
    } else if (strcmp(argv[g], "--map") == 0 || strcmp(argv[g], "-m") == 0) {
      char args_buf[384];
      if (cli_collect_tokens(argc, argv, &g, args_buf, sizeof(args_buf)) != 0) {
        printf("%s switch found, but no map arguments specified.\n", argv[g]);
      } else {
        cli_add_line("map %s", args_buf);
      }
    } else if (strcmp(argv[g], "--mouse") == 0 || strcmp(argv[g], "-M") == 0) {
      if (g + 2 >= argc) {
        printf("%s switch found, but mouse arguments are incomplete.\n", argv[g]);
      } else {
        const char* file = argv[++g];
        const char* key = argv[++g];
        const char* auto_mode = "noauto";
        if (g + 1 < argc && argv[g + 1][0] != '-') {
          auto_mode = argv[++g];
        }
        cli_add_line("mouse %s %s %s", file, key, auto_mode);
      }
    } else if (strcmp(argv[g], "--keyboard") == 0 || strcmp(argv[g], "-K") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but keyboard arguments are incomplete.\n", argv[g]);
      } else {
        const char* key = argv[++g];
        const char* grab = "nograb";
        const char* auto_mode = "noauto";
        if (g + 1 < argc && argv[g + 1][0] != '-') {
          grab = argv[++g];
        }
        if (g + 1 < argc && argv[g + 1][0] != '-') {
          auto_mode = argv[++g];
        }
        cli_add_line("keyboard %s %s %s", key, grab, auto_mode);
      }
    } else if (strcmp(argv[g], "--keyboard-file") == 0 || strcmp(argv[g], "--kbfile") == 0 ||
               strcmp(argv[g], "-k") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no keyboard device path specified.\n", argv[g]);
      } else {
        g++;
        cli_add_line("kbfile %s", argv[g]);
      }
    } else if (strcmp(argv[g], "--platform") == 0 || strcmp(argv[g], "-p") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no platform specified.\n", argv[g]);
      } else {
        const char* plat = argv[++g];
        const char* sub = "";
        if (g + 1 < argc && argv[g + 1][0] != '-') {
          sub = argv[++g];
        }
        if (strlen(sub)) {
          cli_add_line("platform %s %s", plat, sub);
        } else {
          cli_add_line("platform %s", plat);
        }
      }
    } else if (strcmp(argv[g], "--setvar") == 0 || strcmp(argv[g], "-sv") == 0) {
      if (g + 2 >= argc) {
        printf("%s switch found, but setvar arguments are incomplete.\n", argv[g]);
      } else {
        const char* var = argv[++g];
        const char* val = argv[++g];
        cli_add_line("setvar %s %s", var, val);
      }
    }
  }

switch_config:
  ;

#if USE_UAE_JIT
  struct timespec ts_seed;
  clock_gettime(CLOCK_MONOTONIC, &ts_seed);
  srand((unsigned int)(ts_seed.tv_sec ^ ts_seed.tv_nsec));
#endif

  amiga_reset_and_wait("startup");

  if (load_new_config != 0) {
    uint8_t config_action = load_new_config - 1;
    load_new_config = 0;
    if (cfg) {
      free_config_file(cfg);
      free(cfg);
      cfg = NULL;
    }

    switch (config_action) {
    case PICFG_LOAD:
    case PICFG_RELOAD:
      cfg = load_config_file(get_pistorm_devcfg_filename());
      break;
    case PICFG_DEFAULT:
      cfg = load_config_file("default.cfg");
      break;
    }
  }

  if (!cfg) {
    printf("[CFG] No config file specified. Trying to load default.cfg...\n");
    cfg = load_config_file("default.cfg");
    if (!cfg) {
      printf("Couldn't load default.cfg, empty emulator config will be used.\n");
      cfg = (struct emulator_config*)calloc(1, sizeof(struct emulator_config));
      if (!cfg) {
        printf("Failed to allocate memory for emulator config!\n");
        return 1;
      }
      memset(cfg, 0x00, sizeof(struct emulator_config));
    }
  }

  if (cfg) {
    apply_cli_overrides(cfg);
    if (cfg->cpu_type)
      cpu_type = cfg->cpu_type;
    if (cfg->loop_cycles)
      loop_cycles = cfg->loop_cycles;
    if (loop_cycles > loop_cycles_cap) {
      printf("[CFG] loop_cycles capped from %u to %u to reduce latency.\n", loop_cycles,
             loop_cycles_cap);
      loop_cycles = loop_cycles_cap;
    }
    configure_ipl_nops();
    if (!enable_jit_backend && cfg->enable_jit) {
      enable_jit_backend = 1;
      printf("[CFG] JIT backend enabled via config.\n");
    }
    if (!enable_fpu_jit_backend && cfg->enable_fpu_jit) {
      enable_fpu_jit_backend = 1;
      printf("[CFG] FPU JIT backend enabled via config.\n");
    }
    if (enable_fpu_jit_backend) {
      fpu_exec_hook = fpu_backend_execute;
    } else {
      fpu_exec_hook = NULL;
    }

    if (!cfg->platform) {
      cfg->platform = make_platform_config("none", "generic");
    }
    cfg->platform->platform_initial_setup(cfg);
    enforce_kickstart_cpu_compat(cfg);
  }

  if (fc_get_mode() != FC_MODE_OFF) {
    fc_boot_log_init();
    LOG_INFO("[CPU] FC enabled and in use (mode=%s)\n", fc_mode_name(fc_get_mode()));
  } else {
    LOG_INFO("[CPU] FC disabled\n");
  }

  {
    int berr_enabled = read_kernel_param_bool("berr_reset_input");
    if (berr_enabled == 1) {
      LOG_INFO("[CPU][BERR] Reset enabled (berr_reset_input=1)\n");
    } else if (berr_enabled == 0) {
      LOG_INFO("[CPU][BERR] Reset disabled (berr_reset_input=0)\n");
    } else {
      LOG_INFO("[CPU][BERR] Reset status unknown (kernel param not readable)\n");
    }
  }

  if (cfg->mouse_enabled) {
    mouse_fd = open(cfg->mouse_file, O_RDWR | O_NONBLOCK);
    if (mouse_fd == -1) {
      printf("Failed to open %s, can't enable mouse hook.\n", cfg->mouse_file);
      cfg->mouse_enabled = 0;
    } else {
      /**
       * *-*-*-* magic numbers! *-*-*-*
       * great, so waaaay back in the history of the pc, the ps/2 protocol set the standard for mice
       * and in the process, the mouse sample rate was defined as a way of putting mice into
       * vendor-specific modes. as the ancient gpm command explains, almost everything except
       * incredibly old mice talk the IntelliMouse protocol, which reports four bytes. by default,
       * every mouse starts in 3-byte mode ( don't report wheel or additional buttons ) until imps2
       * magic is sent. so, command $f3 is "set sample rate", followed by a byte.
       */
      uint8_t mouse_init[] = {0xf4, 0xf3, 0x64}; // enable, then set sample rate 100
      uint8_t imps2_init[] = {0xf3, 0xc8, 0xf3,
                              0x64, 0xf3, 0x50}; // magic sequence; set sample 200, 100, 80
      if (write(mouse_fd, mouse_init, sizeof(mouse_init)) != -1) {
        if (write(mouse_fd, imps2_init, sizeof(imps2_init)) == -1) {
          printf("[MOUSE] Couldn't enable scroll wheel events; is this mouse from the 1980s?\n");
        }
      } else {
        printf("[MOUSE] Mouse didn't respond to normal PS/2 init; have you plugged a brick in by "
               "mistake?\n");
      }
    }
  }

  if (cfg->keyboard_file) {
    keyboard_fd = open(cfg->keyboard_file, O_RDONLY | O_NONBLOCK);
  } else {
    keyboard_fd = open(keyboard_file, O_RDONLY | O_NONBLOCK);
  }

  if (keyboard_fd == -1) {
    printf("Failed to open keyboard event source.\n");
  }

  if (cfg->mouse_autoconnect) {
    mouse_hook_enabled = 1;
  }

  if (cfg->keyboard_autoconnect) {
    kb_hook_enabled = 1;
  }

  InitGayle();

    struct sigaction sa_int, sa_term;
#if USE_UAE_JIT
  struct sigaction sa_crash;
#endif

  // Setup SIGINT handler for graceful shutdown
  sa_int.sa_handler = sigint_handler;
  sigemptyset(&sa_int.sa_mask);
  sa_int.sa_flags = 0; // Don't restart system calls, allow interruption
  sigaction(SIGINT, &sa_int, NULL);

  // Setup SIGTERM handler for termination
  sa_term.sa_handler = sigint_handler;
  sigemptyset(&sa_term.sa_mask);
  sa_term.sa_flags = 0; // Don't restart system calls, allow interruption
  sigaction(SIGTERM, &sa_term, NULL);

#if USE_UAE_JIT
  // Setup crash signal handlers (JIT diagnostics)
  sa_crash.sa_sigaction = crash_signal_handler_siginfo;
  sigemptyset(&sa_crash.sa_mask);
  sa_crash.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &sa_crash, NULL);
  sigaction(SIGBUS, &sa_crash, NULL);
  sigaction(SIGILL, &sa_crash, NULL);
  sigaction(SIGABRT, &sa_crash, NULL);
#endif


  //amiga_reset_and_wait("pre-cpu");

#ifdef USE_UAE_JIT
  if (enable_jit_backend) {
    int rc = uae_pistorm_init(uae_cpu_model_from_musashi(cpu_type), 1, enable_fpu_jit_backend ? 1 : 0);
    if (rc == 0) {
      use_uae_jit = 1;
      enable_jit_backend = 1;
      LOG_INFO("[CPU] UAE JIT backend enabled\n");
    } else {
      uint32_t rv_sp = 0;
      uint32_t rv_pc = 0;
      int rv_ovl = -1;
      int rv_err = uae_pistorm_get_last_init_error();
      uae_pistorm_get_last_reset_vectors(&rv_sp, &rv_pc, &rv_ovl);
      use_uae_jit = 0;
      enable_jit_backend = 0;
      LOG_ERROR("[CPU] UAE JIT init failed (rc=%d), falling back to Musashi\n", rc);
      LOG_ERROR("[CPU] UAE JIT init detail: err=%d reset_sp=$%.8X reset_pc=$%.8X ovl=%d\n",
                rv_err, rv_sp, rv_pc, rv_ovl);
      if (rv_err == 4) {
        LOG_WARN("[CPU] UAE JIT requires Kickstart ROM at $00F80000 to be Pi-mapped executable memory.\n");
        LOG_WARN("[CPU] Keep Musashi for bus-only motherboard ROM, or map Kickstart in cfg as type=rom.\n");
      }
    }
  }
#endif

  LOG_INFO("[CPU] Active backend: %s\n", use_uae_jit ? "UAE JIT" : "Musashi");

  if (!use_uae_jit) {
    m68k_init();
    printf("Setting CPU type to %d.\n", cpu_type);
    m68k_set_cpu_type(&m68ki_cpu, cpu_type);
    m68k_set_instr_hook_callback(&m68ki_cpu, instr_hook_callback);
    m68k_set_fc_callback(&m68ki_cpu, fc_callback_wrapper);  // Use wrapper to call cpu_set_fc
    m68k_set_illg_instr_callback(&m68ki_cpu, illg_instr_callback);
    cpu_pulse_reset();
  }

  pthread_t ipl_tid = 0, cpu_tid, kbd_tid, mouse_tid = 0;
  int err;

  // When UAE JIT is enabled, keep CPU execution in the main thread.
  // I/O and IRQ service loops still need their own threads; otherwise we block
  // forever in keyboard/mouse polling before the CPU loop starts.
  if (use_uae_jit) {
    printf("[CPU] UAE JIT enabled: running in single-threaded mode\n");

    if (ipl_tid == 0) {
      err = pthread_create(&ipl_tid, NULL, &ipl_task, NULL);
      if (err != 0) {
        printf("[ERROR] Cannot create IPL thread: [%s]", strerror(err));
      } else {
        pthread_setname_np(ipl_tid, "pistorm64: ipl");
        printf("[IPL] Thread created successfully\n");
        apply_affinity_from_env("ipl", CORE_IPL);
        apply_realtime_from_env("ipl", RT_DEFAULT_IPL);
      }
    }

    err = pthread_create(&kbd_tid, NULL, &keyboard_task, NULL);
    if (err != 0) {
      printf("[ERROR] Cannot create keyboard thread: [%s]", strerror(err));
    } else {
      pthread_setname_np(kbd_tid, "pistorm64: kbd");
      printf("[MAIN] Keyboard thread created successfully\n");
      apply_affinity_from_env("input", CORE_INPUT);
    }

    if (mouse_fd != -1) {
      err = pthread_create(&mouse_tid, NULL, &mouse_task, NULL);
      if (err != 0) {
        printf("[ERROR] Cannot create mouse thread: [%s]", strerror(err));
      } else {
        pthread_setname_np(mouse_tid, "pistorm64: mouse");
        printf("[MAIN] Mouse thread created successfully\n");
        apply_affinity_from_env("input", CORE_INPUT);
      }
    }

    // Run CPU task in main thread (this will be the only thread doing CPU emulation)
    cpu_task(NULL);
  } else {
    if (ipl_tid == 0) {
      err = pthread_create(&ipl_tid, NULL, &ipl_task, NULL);
      if (err != 0) {
        printf("[ERROR] Cannot create IPL thread: [%s]", strerror(err));
      } else {
        pthread_setname_np(ipl_tid, "pistorm64: ipl");
        printf("[IPL] Thread created successfully\n");
        apply_affinity_from_env("ipl", CORE_IPL);
        apply_realtime_from_env("ipl", RT_DEFAULT_IPL);
      }
    }

    // create keyboard task
    err = pthread_create(&kbd_tid, NULL, &keyboard_task, NULL);
    if (err != 0) {
      printf("[ERROR] Cannot create keyboard thread: [%s]", strerror(err));
    } else {
      pthread_setname_np(kbd_tid, "pistorm64: kbd");
      printf("[MAIN] Keyboard thread created successfully\n");
      apply_affinity_from_env("input", CORE_INPUT);
    }

    // create mouse task if mouse is enabled
    if (mouse_fd != -1) {
      err = pthread_create(&mouse_tid, NULL, &mouse_task, NULL);
      if (err != 0) {
        printf("[ERROR] Cannot create mouse thread: [%s]", strerror(err));
      } else {
        pthread_setname_np(mouse_tid, "pistorm64: mouse");
        printf("[MAIN] Mouse thread created successfully\n");
        apply_affinity_from_env("input", CORE_INPUT);
      }
    }

    // create cpu task
    err = pthread_create(&cpu_tid, NULL, &cpu_task, NULL);
    if (err != 0) {
      printf("[ERROR] Cannot create CPU thread: [%s]", strerror(err));
    } else {
      pthread_setname_np(cpu_tid, "pistorm64: cpu");
      printf("[MAIN] CPU thread created successfully\n");
      apply_affinity_from_env("cpu", CORE_CPU);
    }

    // wait for cpu task to end before closing up and finishing
    // Use a polling approach to allow signal handling
    while (!end_signal) {
      // Sleep briefly to allow signal processing
      usleep(50000); // Sleep 50ms

      // Check if the CPU thread has finished by using pthread_kill
      // If the thread is still running, pthread_kill will return 0
      int kill_result = pthread_kill(cpu_tid, 0);
      if (kill_result != 0) {
        // Thread has probably finished (ESRCH error)
        break;
      }
      // If thread is still running, continue loop to check end_signal
    }
  }

  if (sigint_seen) {
    printf("IRQs triggered: %lu\n", (unsigned long)trig_irq);
    printf("IRQs serviced: %lu\n", (unsigned long)serv_irq);
    printf("Last serviced IRQ: %d\n", last_last_irq);
  }

  while (!emulator_exiting) {
    emulator_exiting = 1;
    usleep(0);
  }

  if (load_new_config == 0) {
    printf("[MAIN] All threads appear to have concluded; ending process\n");
  }

  if (mouse_fd != -1) {
    close(mouse_fd);
  }
  if (mem_fd) {
    close(mem_fd);
  }

  if (load_new_config != 0) {
    goto switch_config;
  }

  // Join other threads with timeouts
  struct timespec other_timeout;
  clock_gettime(CLOCK_MONOTONIC, &other_timeout);
  other_timeout.tv_sec += 2; // 2 second timeout

  if (kbd_tid) {
    pthread_timedjoin_np(kbd_tid, NULL, &other_timeout);
  }
  if (mouse_tid && mouse_fd != -1) {
    pthread_timedjoin_np(mouse_tid, NULL, &other_timeout);
  }
  if (ipl_tid) {
    pthread_timedjoin_np(ipl_tid, NULL, &other_timeout);
  }

  if (cfg->platform->shutdown) {
    cfg->platform->shutdown(cfg);
  }

  #ifdef PS_PROTOCOL_HAS_CLEANUP
#ifdef PS_PROTOCOL_HAS_CLEANUP
  ps_cleanup_protocol();
#endif
  #endif

  ps_protocol_dump_stats();

  return 0;
}

void cpu_pulse_reset(void) {
  m68ki_cpu_core* state = &m68ki_cpu;
  ps_pulse_reset();

  ovl = 1;
  m68ki_cpu.ovl = 1;
  for (int i = 0; i < 8; i++) {
    ipl_enabled[i] = 0;
  }

  if (cfg->platform->handle_reset) {
    cfg->platform->handle_reset(cfg);
  }

  m68k_pulse_reset(state);
}

unsigned int cpu_irq_ack(int level) {
  // printf( "cpu irq ack\n" );
  return (unsigned int)(24 + level);
}

void cpu_instr_callback(unsigned int pc) {
    (void)pc;
    /* Optional: record last PC(s) in a ring buffer for later debugging */
}

static unsigned int target = 0;
static uint32_t platform_res, rres;

uint8_t cdtv_dmac_reg_idx_read(void);
void cdtv_dmac_reg_idx_write(uint8_t value);
uint32_t cdtv_dmac_read(uint32_t address, uint8_t type);
void cdtv_dmac_write(uint32_t address, uint32_t value, uint8_t type);

unsigned int garbage = 0;

static inline uint32_t ps_read(uint8_t type, uint32_t addr) {
  switch (type) {
  case OP_TYPE_BYTE:
    return (uint32_t)ps_read_8(addr);
  case OP_TYPE_WORD:
    return (uint32_t)ps_read_16(addr);
  case OP_TYPE_LONGWORD:
    if (addr & 0x01) {
      uint32_t c = (uint32_t)ps_read_8(addr);
      c |= ((uint32_t)be16toh(ps_read_16(addr + 1)) << 8);
      c |= ((uint32_t)ps_read_8(addr + 3) << 24);
      return htobe32(c);
    }
    {
      uint32_t a = (uint32_t)ps_read_16(addr);
      uint32_t b = (uint32_t)ps_read_16(addr + 2);
      return (a << 16) | b;
    }
  }
  // This shouldn't actually happen.
  return 0;
}

static inline void ps_write(uint8_t type, uint32_t addr, uint32_t val) {
  switch (type) {
  case OP_TYPE_BYTE:
    ps_write_8(addr, (uint8_t)val);
    return;
  case OP_TYPE_WORD:
    ps_write_16(addr, (uint16_t)val);
    return;
  case OP_TYPE_LONGWORD:
    if (addr & 0x01) {
      ps_write_8(addr, (uint8_t)(val & 0xFF));
      ps_write_16(addr + 1, htobe16((uint16_t)((val >> 8) & 0xFFFF)));
      ps_write_8(addr + 3, (uint8_t)(val >> 24));
      return;
    }
    ps_write_16(addr, (uint16_t)(val >> 16));
    ps_write_16(addr + 2, (uint16_t)val);
    return;
  }
  // This shouldn't actually happen.
  return;
}





static inline int32_t platform_read_check(uint8_t type, uint32_t addr, uint32_t* res) {
  switch (cfg->platform->id) {
  case PLATFORM_AMIGA:
    switch (addr) {
    case INTREQR:
      return amiga_handle_intrqr_read(res);
      break;
    case CIAAPRA:
      if (mouse_hook_enabled) {
        uint8_t buttons = (uint8_t)(mouse_buttons | mouse_buttons_latched);
        if (buttons & 0x01) {
          if ((mouse_buttons_latched & 0x01) && !(mouse_buttons & 0x01)) {
            mouse_buttons_latched &= (uint8_t)~0x01u;
          }
          rres = (uint32_t)ps_read(type, addr);
          *res = (rres ^ 0x40);
          return 1;
        }
      }
      if (swap_df0_with_dfx && spoof_df0_id) {
        // DF0 doesn't emit a drive type ID on RDY pin
        // If swapping DF0 with DF1-3 we need to provide this ID so that DF0 continues to function.
        rres = (uint32_t)ps_read(type, addr);
        *res = (rres & 0xDF); // Spoof drive id for swapped DF0 by setting RDY low
        return 1;
      }
      return 0;
      break;
    case CIAAICR:
      if (kb_hook_enabled && get_num_kb_queued() && amiga_emulating_irq(PORTS)) {
        *res = 0x88;
        return 1;
      }
      return 0;
      break;
    case CIAADAT:
      if (kb_hook_enabled && amiga_emulating_irq(PORTS)) {
        uint8_t c = 0, t = 0;
        pop_queued_key(&c, &t);
        t ^= 0x01;
        rres = (uint32_t)((((uint32_t)c << 1) | t) ^ 0xFFu);
        *res = rres;
        return 1;
      }
      return 0;
      break;
    case JOY0DAT:
      if (mouse_hook_enabled) {
        uint16_t result = (uint16_t)(((uint16_t)mouse_dy << 8) | mouse_dx);
        *res = (uint32_t)result;
        return 1;
      }
      return 0;
      break;
    case INTENAR: {
      // This code is kind of strange and should probably be reworked/revoked.
      uint8_t enable = 1;
      rres = (uint16_t)ps_read(type, addr);
      uint16_t val = (uint16_t)rres;
      if (val & 0x0007) {
        ipl_enabled[1] = enable;
      }
      if (val & 0x0008) {
        ipl_enabled[2] = enable;
      }
      if (val & 0x0070) {
        ipl_enabled[3] = enable;
      }
      if (val & 0x0780) {
        ipl_enabled[4] = enable;
      }
      if (val & 0x1800) {
        ipl_enabled[5] = enable;
      }
      if (val & 0x2000) {
        ipl_enabled[6] = enable;
      }
      if (val & 0x4000) {
        ipl_enabled[7] = enable;
      }
      // printf( "Interrupts enabled: M:%d 0-6:%d%d%d%d%d%d\n", ipl_enabled[7], ipl_enabled[6],
      // ipl_enabled[5], ipl_enabled[4], ipl_enabled[3], ipl_enabled[2], ipl_enabled[1] );
      *res = rres;
      return 1;
      break;
    }
    case POTGOR:
      if (mouse_hook_enabled) {
        uint8_t buttons = (uint8_t)(mouse_buttons | mouse_buttons_latched);
        unsigned short result = (unsigned short)ps_read(type, addr);
        // bit 1 rmb, bit 2 mmb
        if (buttons & 0x06) {
          if ((mouse_buttons_latched & 0x02) && !(mouse_buttons & 0x02)) {
            mouse_buttons_latched &= (uint8_t)~0x02u;
          }
          if ((mouse_buttons_latched & 0x04) && !(mouse_buttons & 0x04)) {
            mouse_buttons_latched &= (uint8_t)~0x04u;
          }
          *res = (unsigned int)((result ^ ((buttons & 0x02) << 9))     // move rmb to bit 10
                                & (result ^ ((buttons & 0x04) << 6))); // move mmb to bit 8
          return 1;
        }
        *res = (unsigned int)(result & 0xfffd);
        return 1;
      }
      return 0;
      break;
    case CIABPRB:
      if (swap_df0_with_dfx) {
        uint32_t result = (uint32_t)ps_read(type, addr);
        // SEL0 = 0x80, SEL1 = 0x10, SEL2 = 0x20, SEL3 = 0x40
        if (((result >> SEL0_BITNUM) & 1) != ((result >> (SEL0_BITNUM + swap_df0_with_dfx)) & 1)) {
          // If the value for SEL0/SELx differ
          result ^= ((1 << SEL0_BITNUM) | (1 << (SEL0_BITNUM + swap_df0_with_dfx)));
          // Invert both bits to swap them around
        }
        *res = result;
        return 1;
      }
      return 0;
      break;
    default:
      break;
    }

    if (move_slow_to_chip && addr >= 0x080000 && addr <= 0x0FFFFF) {
      // A500 JP2 connects Agnus' A19 input to A23 instead of A19 by default, and decodes trapdoor
      // memory at 0xC00000 instead of 0x080000. We can move the trapdoor to chipram simply by
      // rewriting the address.
      addr += 0xB80000;
      *res = ps_read(type, addr);
      return 1;
    }

    if (move_slow_to_chip && addr >= 0xC00000 && addr <= 0xC7FFFF) {
      // Block accesses through to trapdoor at slow ram address, otherwise it will be detected at
      // 0x080000 and 0xC00000.
      *res = 0;
      return 1;
    }

    if (addr >= cfg->custom_low && addr < cfg->custom_high) {
      if (addr >= PISCSI_OFFSET && addr < PISCSI_UPPER) {
        *res = handle_piscsi_read(addr, type);
        return 1;
      }
      if (addr >= PINET_OFFSET && addr < PINET_UPPER) {
        *res = handle_pinet_read(addr, type);
        return 1;
      }
      if (addr >= RTG_BASE && addr < RTG_UPPER) {
        *res = rtg_read((addr & 0x0FFFFFFF), type);
        return 1;
      }
      if (addr >= PI_AHI_OFFSET && addr < PI_AHI_UPPER) {
        *res = handle_pi_ahi_read(addr, type);
        return 1;
      }
      if (custom_read_amiga(cfg, addr, &target, type) != -1) {
        *res = target;
        return 1;
      }
    }

    break;
  default:
    break;
  }

  if (ovl || (addr >= cfg->mapped_low && addr < cfg->mapped_high)) {
    if (handle_mapped_read(cfg, addr, &target, type) != -1) {
      *res = target;
      return 1;
    }
  }

  return 0;
}

unsigned int m68k_read_memory_8(unsigned int address) {
  fc_shadow_touch(OP_TYPE_BYTE, address, 0);
  if (platform_read_check(OP_TYPE_BYTE, address, &platform_res)) {
    lowvec_trace_log("R08 platform", address, platform_res & 0xFF);
    mem_trace_log("R08", address, platform_res & 0xFFu, 1u, "platform");
    return platform_res;
  }

  if (address & 0xFF000000) {
    lowvec_trace_log("R08 highmask", address, 0);
    mem_trace_log("R08", address, 0u, 1u, "highmask");
    return 0;
  }

  unsigned int v = (unsigned int)ps_read_8((uint32_t)address);
  lowvec_trace_log("R08 ps", address, v & 0xFF);
  mem_trace_log("R08", address, v & 0xFFu, 1u, "ps");
  return v;
}

unsigned int m68k_read_memory_16(unsigned int address) {
  fc_shadow_touch(OP_TYPE_WORD, address, 0);
  if ((address & 0x01) && log_get_level() >= LOG_LEVEL_VERBOSE) {
    LOG_ERROR("[ALIGN] read16 addr=$%.8X PC=$%.8X\n",
              address, cpu_backend_get_pc());
  }
  if (platform_read_check(OP_TYPE_WORD, address, &platform_res)) {
    lowvec_trace_log("R16 platform", address, platform_res & 0xFFFF);
    mem_trace_log("R16", address, platform_res & 0xFFFFu, 2u, "platform");
    return platform_res;
  }

  if (address & 0xFF000000) {
    lowvec_trace_log("R16 highmask", address, 0);
    mem_trace_log("R16", address, 0u, 2u, "highmask");
    return 0;
  }

  unsigned int v;
  if (address & 0x01) {
    v = ((unsigned int)(ps_read_8(address) << 8) | (unsigned int)ps_read_8(address + 1));
    lowvec_trace_log("R16 ps-odd", address, v & 0xFFFF);
    mem_trace_log("R16", address, v & 0xFFFFu, 2u, "ps-odd");
    return v;
  }
  v = (unsigned int)ps_read_16((uint32_t)address);
  lowvec_trace_log("R16 ps", address, v & 0xFFFF);
  mem_trace_log("R16", address, v & 0xFFFFu, 2u, "ps");
  return v;
}

unsigned int m68k_read_memory_32(unsigned int address) {
  fc_shadow_touch(OP_TYPE_LONGWORD, address, 0);
  if ((address & 0x03) && log_get_level() >= LOG_LEVEL_VERBOSE) {
    LOG_ERROR("[ALIGN] read32 addr=$%.8X PC=$%.8X\n",
              address, cpu_backend_get_pc());
  }
  if (platform_read_check(OP_TYPE_LONGWORD, address, &platform_res)) {
    lowvec_trace_log("R32 platform", address, platform_res);
    mem_trace_log("R32", address, platform_res, 4u, "platform");
    return platform_res;
  }

  if (address & 0xFF000000) {
    lowvec_trace_log("R32 highmask", address, 0);
    mem_trace_log("R32", address, 0u, 4u, "highmask");
    return 0;
  }

  unsigned int v;
  if (address & 0x01) {
    v = (unsigned int)ps_read(OP_TYPE_LONGWORD, address);
    lowvec_trace_log("R32 ps-odd", address, v);
    mem_trace_log("R32", address, v, 4u, "ps-odd");
    return v;
  }
  v = (unsigned int)ps_read(OP_TYPE_LONGWORD, address);
  lowvec_trace_log("R32 ps", address, v);
  mem_trace_log("R32", address, v, 4u, "ps");
  return v;
}

static inline int32_t platform_write_check(uint8_t type, uint32_t addr, uint32_t val) {
  switch (cfg->platform->id) {
  case PLATFORM_MAC:
    switch (addr) {
    case 0xEFFFFE: // VIA1?
      if (val & 0x10 && !ovl) {
        ovl = 1;
        m68ki_cpu.ovl = 1;
        printf("[MAC] OVL on.\n");
        handle_ovl_mappings_mac68k(cfg);
      } else if (ovl) {
        ovl = 0;
        m68ki_cpu.ovl = 0;
        printf("[MAC] OVL off.\n");
        handle_ovl_mappings_mac68k(cfg);
      }
      break;
    }
    break;
  case PLATFORM_AMIGA:
    switch (addr) {
    case INTREQ:
      return amiga_handle_intrq_write(val);
      break;
    case CIAAPRA:
      if (ovl != (val & (1 << 0))) {
        ovl = (val & (1 << 0));
        m68ki_cpu.ovl = ovl;
        printf("OVL:%x", ovl);
#ifdef USE_UAE_JIT
        if (use_uae_jit) {
          uae_pistorm_overlay_changed((int)ovl);
          printf(" (uae_pc=%08X regs.pc=%08X regs.pc_p=%08X)",
                 uae_pistorm_get_pc(),
                 uae_pistorm_get_regs_pc(),
                 uae_pistorm_get_regs_pc_p());
        }
#endif
        printf("\n");
      }
      return 0;
      break;
    case SERDAT: {
      char* serdat = (char*)&val;
      // SERDAT word. see amiga dev docs appendix a; upper byte is control codes, and bit 0 is
      // always 1. ignore this upper byte as it's not viewable data, only display lower byte.
      printf("%c", serdat[0]);
      return 0;
      break;
    }
    case INTENA: {
      // This code is kind of strange and should probably be reworked/revoked.
      uint8_t enable = 1;
      if (!(val & 0x8000)) {
        enable = 0;
      }
      if (val & 0x0007) {
        ipl_enabled[1] = enable;
      }
      if (val & 0x0008) {
        ipl_enabled[2] = enable;
      }
      if (val & 0x0070) {
        ipl_enabled[3] = 1;
      }
      if (val & 0x0780) {
        ipl_enabled[4] = enable;
      }
      if (val & 0x1800) {
        ipl_enabled[5] = enable;
      }
      if (val & 0x2000) {
        ipl_enabled[6] = enable;
      }
      if (val & 0x4000 && enable) {
        ipl_enabled[7] = 1;
      }
      // printf( "Interrupts enabled: M:%d 0-6:%d%d%d%d%d%d\n", ipl_enabled[7], ipl_enabled[6],
      // ipl_enabled[5], ipl_enabled[4], ipl_enabled[3], ipl_enabled[2], ipl_enabled[1] );
      return 0;
      break;
    }
    case CIABPRB:
      if (swap_df0_with_dfx) {
        if ((val & ((1 << (SEL0_BITNUM + swap_df0_with_dfx)) | 0x80)) == 0x80) {
          // If drive selected but motor off, Amiga is reading drive ID.
          spoof_df0_id = 1;
        } else {
          spoof_df0_id = 0;
        }

        if (((val >> SEL0_BITNUM) & 1) != ((val >> (SEL0_BITNUM + swap_df0_with_dfx)) & 1)) {
          // If the value for SEL0/SELx differ
          val ^= ((1 << SEL0_BITNUM) | (1 << (SEL0_BITNUM + swap_df0_with_dfx)));
          // Invert both bits to swap them around
        }
        ps_write(type, addr, val);
        return 1;
      }
      return 0;
      break;
    default:
      break;
    }

    if (move_slow_to_chip && addr >= 0x080000 && addr <= 0x0FFFFF) {
      // A500 JP2 connects Agnus' A19 input to A23 instead of A19 by default, and decodes trapdoor
      // memory at 0xC00000 instead of 0x080000. We can move the trapdoor to chipram simply by
      // rewriting the address.
      addr += 0xB80000;
      ps_write(type, addr, val);
      return 1;
    }

    if (move_slow_to_chip && addr >= 0xC00000 && addr <= 0xC7FFFF) {
      // Block accesses through to trapdoor at slow ram address, otherwise it will be detected at
      // 0x080000 and 0xC00000.
      return 1;
    }

    if (addr >= cfg->custom_low && addr < cfg->custom_high) {
      if (addr >= PISCSI_OFFSET && addr < PISCSI_UPPER) {
        handle_piscsi_write(addr, val, type);
        return 1;
      }
      if (addr >= PINET_OFFSET && addr < PINET_UPPER) {
        handle_pinet_write(addr, val, type);
        return 1;
      }
      if (addr >= RTG_BASE && addr < RTG_UPPER) {
        rtg_write((addr & 0x0FFFFFFF), val, type);
        return 1;
      }
      if (addr >= PI_AHI_OFFSET && addr < PI_AHI_UPPER) {
        handle_pi_ahi_write(addr, val, type);
        return 1;
      }
      if (custom_write_amiga(cfg, addr, val, type) != -1) {
        return 1;
      }
    }

    break;
  default:
    break;
  }

  if (ovl || (addr >= cfg->mapped_low && addr < cfg->mapped_high)) {
    if (handle_mapped_write(cfg, addr, val, type) != -1) {
      return 1;
    }
  }

  return 0;
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
  fc_shadow_touch(OP_TYPE_BYTE, address, 1);
  lowvec_trace_log("W08 req", address, value & 0xFF);
  if (platform_write_check(OP_TYPE_BYTE, address, value)) {
    lowvec_trace_log("W08 platform", address, value & 0xFF);
    mem_trace_log("W08", address, value & 0xFFu, 1u, "platform");
    return;
  }

  if (address & 0xFF000000) {
    lowvec_trace_log("W08 highmask", address, value & 0xFF);
    mem_trace_log("W08", address, value & 0xFFu, 1u, "highmask");
    return;
  }

  ps_write_8((uint32_t)address, (uint8_t)value);
  lowvec_trace_log("W08 ps", address, value & 0xFF);
  mem_trace_log("W08", address, value & 0xFFu, 1u, "ps");
  return;
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
  fc_shadow_touch(OP_TYPE_WORD, address, 1);
  lowvec_trace_log("W16 req", address, value & 0xFFFF);
  if ((address & 0x01) && log_get_level() >= LOG_LEVEL_VERBOSE) {
    LOG_ERROR("[ALIGN] write16 addr=$%.8X val=$%.4X PC=$%.8X\n",
              address, value & 0xFFFF, cpu_backend_get_pc());
  }
  if (platform_write_check(OP_TYPE_WORD, address, value)) {
    lowvec_trace_log("W16 platform", address, value & 0xFFFF);
    mem_trace_log("W16", address, value & 0xFFFFu, 2u, "platform");
    return;
  }

  if (address & 0xFF000000) {
    lowvec_trace_log("W16 highmask", address, value & 0xFFFF);
    mem_trace_log("W16", address, value & 0xFFFFu, 2u, "highmask");
    return;
  }

  if (address & 0x01) {
    ps_write_8((uint32_t)address, (uint8_t)(value & 0xFF));
    ps_write_8((uint32_t)address + 1, (uint8_t)((value >> 8) & 0xFF));
    lowvec_trace_log("W16 ps-odd", address, value & 0xFFFF);
    mem_trace_log("W16", address, value & 0xFFFFu, 2u, "ps-odd");
    return;
  }

  ps_write_16((uint32_t)address, (uint16_t)value);
  lowvec_trace_log("W16 ps", address, value & 0xFFFF);
  mem_trace_log("W16", address, value & 0xFFFFu, 2u, "ps");
  return;
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
  fc_shadow_touch(OP_TYPE_LONGWORD, address, 1);
  lowvec_trace_log("W32 req", address, value);
  if ((address & 0x03) && log_get_level() >= LOG_LEVEL_VERBOSE) {
    LOG_ERROR("[ALIGN] write32 addr=$%.8X val=$%.8X PC=$%.8X\n",
              address, value, cpu_backend_get_pc());
  }
  if (platform_write_check(OP_TYPE_LONGWORD, address, value)) {
    lowvec_trace_log("W32 platform", address, value);
    mem_trace_log("W32", address, value, 4u, "platform");
    return;
  }

  if (address & 0xFF000000) {
    lowvec_trace_log("W32 highmask", address, value);
    mem_trace_log("W32", address, value, 4u, "highmask");
    return;
  }

  if (address & 0x01) {
    ps_write(OP_TYPE_LONGWORD, address, (uint32_t)value);
    lowvec_trace_log("W32 ps-odd", address, value);
    mem_trace_log("W32", address, value, 4u, "ps-odd");
    return;
  }

  ps_write(OP_TYPE_LONGWORD, address, (uint32_t)value);
  lowvec_trace_log("W32 ps", address, value);
  mem_trace_log("W32", address, value, 4u, "ps");
  return;
}

void m68k_write_memory_32_pd(unsigned int address, unsigned int value) {
    /* Simulate 68k predecrement MOVE.L write ordering:
     *
     *   1) write high word to [address + 2]
     *   2) write low  word to [address]
     *
     * Reuse the existing 16-bit path so all the platform checks,
     * logging and ps_protocol plumbing stay consistent.
     */

    uint16_t hi = (uint16_t)((value >> 16) & 0xFFFF);
    uint16_t lo = (uint16_t)( value        & 0xFFFF);

    /* High word first at address+2 */
    m68k_write_memory_16(address + 2, hi);

    /* Then low word at address */
    m68k_write_memory_16(address, lo);
}

static void set_affinity_for(const char* name, int core_id) {
  if (core_id < 0) {
    return;
  }

  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(core_id, &set);
  pthread_t self = pthread_self();
  if (pthread_setaffinity_np(self, sizeof(set), &set) != 0) {
    printf("[AFF] Failed to set affinity for %s to core %d\n", name, core_id);
  } else {
    printf("[AFF] %s pinned to core %d\n", name, core_id);
  }
}

static int role_has_input_fallback(const char* role) {
  return (strcmp(role, "keyboard") == 0 || strcmp(role, "mouse") == 0);
}

static int key_matches_role(const char* role, const char* key) {
  if (strcasecmp(key, role) == 0) {
    return 1;
  }
  if (strcmp(role, "keyboard") == 0 && strcasecmp(key, "kbd") == 0) {
    return 1;
  }
  return 0;
}

static int realtime_allowed(void) {
  if (geteuid() == 0) {
    return 1;
  }

  struct rlimit lim;
  if (getrlimit(RLIMIT_RTPRIO, &lim) == 0 && lim.rlim_cur > 0) {
    return 1;
  }

  return 0;
}

static void apply_affinity_from_env(const char* role, int default_core) {
  int target_core = default_core;
  int fallback = -1;
  int matched = 0;
  const char* env = getenv(PI_AFFINITY_ENV);
  if (env && strlen(env)) {
    // parse simple comma list key=val
    char* dup = strdup(env);
    char* tok = strtok(dup, ", ");
    while (tok) {
      char key[16];
      int val = -1;
      if (sscanf(tok, "%15[^=]=%d", key, &val) == 2) {
        if (key_matches_role(role, key)) {
          target_core = val;
          matched = 1;
        }
        if (!matched && role_has_input_fallback(role) && strcasecmp(key, "input") == 0) {
          fallback = val;
        }
      }
      tok = strtok(NULL, ", ");
    }
    free(dup);
  }
  if (!matched && fallback >= 0) {
    target_core = fallback;
  }
  set_affinity_for(role, target_core);
}

static void set_realtime_priority(const char* name, int prio) {
  int maxp = sched_get_priority_max(SCHED_RR);
  int minp = sched_get_priority_min(SCHED_RR);
  if (prio < minp)
    prio = minp;
  if (prio > maxp)
    prio = maxp;

  struct sched_param sp;
  memset(&sp, 0, sizeof(sp));
  sp.sched_priority = prio;
  if (pthread_setschedparam(pthread_self(), SCHED_RR, &sp) != 0) {
    if (errno == EPERM) {
      printf("[PRIO] RT priority for %s denied ( CAP_SYS_NICE/rtprio limit needed )\n", name);
    } else {
      printf("[PRIO] Failed to set RT priority for %s ( %s )\n", name, strerror(errno));
    }
  } else {
    printf("[PRIO] %s set to SCHED_RR priority %d\n", name, prio);
  }
}

static void apply_realtime_from_env(const char* role, int default_prio) {
  static int rt_warned;
  int allowed = realtime_allowed();
  int target_prio = default_prio;
  int fallback = -1;
  int matched = 0;
  const char* env = getenv(PI_RT_ENV);
  if (env && strlen(env)) {
    char* dup = strdup(env);
    char* tok = strtok(dup, ", ");
    while (tok) {
      char key[16];
      int val = -1;
      if (sscanf(tok, "%15[^=]=%d", key, &val) == 2) {
        if (key_matches_role(role, key)) {
          target_prio = val;
          matched = 1;
        }
        if (!matched && role_has_input_fallback(role) && strcasecmp(key, "input") == 0) {
          fallback = val;
        }
      }
      tok = strtok(NULL, ", ");
    }
    free(dup);
    if (!matched && fallback >= 0) {
      target_prio = fallback;
    }
  } else if (!allowed) {
    return;
  }

  if (!allowed) {
    if (!rt_warned) {
      printf("[PRIO] RT scheduling disabled (no CAP_SYS_NICE/RLIMIT_RTPRIO).\n");
      rt_warned = 1;
    }
    return;
  }

  if (target_prio > 0) {
    set_realtime_priority(role, target_prio);
  }
}

static void cli_add_line(const char* fmt, ...) {
  if (cli_config_count >= CLI_MAX_LINES) {
    printf("[CLI] Too many config overrides; ignoring additional entries.\n");
    return;
  }

  char buf[512];
  va_list args;
  va_start(args, fmt);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
  vsnprintf(buf, sizeof(buf), fmt, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  va_end(args);

  cli_config_lines[cli_config_count++] = strdup(buf);
}

static void apply_cli_overrides(struct emulator_config* cfg_local) {
  if (!cfg_local || cli_config_count == 0) {
    return;
  }

  for (int i = 0; i < cli_config_count; i++) {
    if (strncmp(cli_config_lines[i], "platform ", 9) == 0) {
      apply_config_line(cfg_local, cli_config_lines[i], 0);
    }
  }

  for (int i = 0; i < cli_config_count; i++) {
    if (strncmp(cli_config_lines[i], "platform ", 9) == 0 ||
        strncmp(cli_config_lines[i], "setvar ", 7) == 0) {
      continue;
    }
    apply_config_line(cfg, cli_config_lines[i], 0);
  }

  for (int i = 0; i < cli_config_count; i++) {
    if (strncmp(cli_config_lines[i], "setvar ", 7) == 0) {
      apply_config_line(cfg_local, cli_config_lines[i], 0);
    }
  }
}

static int cli_collect_tokens(int argc, char* argv[], int* index, char* out, size_t out_len) {
  size_t pos = 0;

  if (!out || out_len == 0)
    return -1;

  out[0] = '\0';

  while (*index + 1 < argc) {
    const char* tok = argv[*index + 1];
    if (tok[0] == '-')
      break;
    size_t len = strlen(tok);
    if (pos + len + 2 > out_len)
      return -1;
    if (pos) {
      out[pos++] = ' ';
    }
    memcpy(out + pos, tok, len);
    pos += len;
    out[pos] = '\0';
    (*index)++;
  }

  return pos ? 0 : -1;
}

static void print_about(const char* prog) {
  printf("KERNEL PiStorm64 - JANUS BUS ENGINE\n");
  printf("-----------------------------------\n");
  printf("KERNEL PiStorm64 is a fork of the PiStorm emulator stack, turning a Raspberry Pi\n");
  printf("into a Janus-style bus engine for classic Amiga machines.\n");
  printf("\n");
  printf("Focus areas:\n");
  printf("- Clean, hardened memory mapping for Z2/Z3 and CPU-local Fast RAM\n");
  printf("- RTG (PiGFX / Picasso96) using Pi-side VRAM\n");
  printf("- LibRemote networking, PiSCSI-backed storage, and co-processor services\n");
  printf("- Deterministic timing, CPU affinity, and RT priorities on the Pi\n");
  printf("\n");
  printf("Upstream and component credits:\n");
  printf("- PiStorm original project and Amiga platform work:\n");
  printf("  captain-amygdala and contributors\n");
  printf("- CPU emulation:\n");
  printf("  \"Musashi\" 680x0 core by Karl Stenerud\n");
  printf("- Floating-point emulation:\n");
  printf("  SoftFloat by John R. Hauser (via MAME-derived milieu)\n");
  printf("- Storage and SCSI emulation:\n");
  printf("  PiSCSI / Dayna / SCSI code and contributors\n");
  printf("- A314 / Amiga-Pi bridge and CPLD foundations:\n");
  printf("  A314 designed and developed by Niklas Ekstrom,\n");
  printf("  whose work also underpins the CPLD logic used in PiStorm-class hardware\n");
  printf("- RTG / PiGFX:\n");
  printf("  Picasso96 authors and PiGFX-related contributors in the PiStorm tree\n");
  printf("\n");
  printf("This fork adds:\n");
  printf("- A clearer src/platforms layout for Amiga-focused work\n");
  printf("- A CLI front end for config, threading, and JIT control\n");
  printf("- Tightened types, memory ranges, and autoconf handling for large Z3 maps\n");
  printf("- Experiments with Pi-side co-processor style services (JANUS bus engine)\n");
  printf("- FC/BERR plumbing for CPLD-aware bus cycles and reset handling\n");
  printf("\n");
  printf("Runtime hints:\n");
  printf("- Kernel module: gpclk_src/gpclk_div, berr_reset_input, run_batch_enable, bus_arb_release\n");
  printf("- Userspace queue: PISTORM_ENABLE_QUEUE=1 (optional PISTORM_BATCH_BITS=2048)\n");
  printf("\n");
  printf("Project goals:\n");
  printf("- Treat the Pi as a disciplined hardware companion, not just a blunt accelerator\n");
  printf("- Make Fast RAM, RTG, and Pi-side services feel \"native\" to the Amiga\n");
  printf("- Keep behaviour reproducible and tunable for both benchmarking and real use\n");
  printf("\n");
  printf("Tooling and assistance:\n");
  printf("- Built with GCC/Clang, Make, vim, perf, bustest, and assorted diagnostic tools\n");
  printf("- Heavy use of AI code assistants (Qwen / Codex / GPT-style),\n");
  printf("  acting as \"compiler + IDE + static analyser + rubber duck... with a mouth.\"\n");
  printf("- All architecture decisions, hardware behaviour assumptions, and final code\n");
  printf("  are curated, reviewed, and tested by the human maintainers.\n");
  printf("\n");
  printf("Legal:\n");
  printf("- Copyright (c) 2026 AKADATA LIMITED - Kernel PiStorm64 (pistorm.ko portions)\n");
  printf("- All trademarks are property of their respective owners.\n");
  printf("- This software is provided under the terms of its source license; see LICENSE.\n");
  printf("\n");
  printf("Usage: %s [options]\n", prog);
}

static void print_help(const char* prog) {
  print_about(prog);
  printf("\n");
  printf("General:\n");
  printf("  -h, --help                 Show this help and exit\n");
  printf("  -a, --about                Show about info and exit\n");
  printf("  --log [file]               Write log output to file (default: amiga.log)\n");
  printf("  --syslog                   Send log output to syslog/systemd\n");
  printf("  -l, --log-level <level>    Set log level (error|warn|info|debug|verbose)\n");
  printf("  --debug-level <level>      Alias for --log-level\n");
  printf("  --affinity <spec>          Thread affinity (e.g., cpu=3,ipl=2,keyboard=1,mouse=1)\n");
  printf("  --rtprio <spec>            RT priorities (SCHED_RR, e.g., cpu=80,ipl=70,keyboard=90)\n");
  printf("\n");
  printf("Config (.cfg equivalents):\n");
  printf("  -c, --config <file>        Load config file\n");
  printf("  -C, --cpu <type>           CPU type (e.g., 68000, 68020)\n");
  printf("  -L, --loopcycles <n>       CPU loop cycles\n");
  printf("  -j, --jit                  Enable JIT backend\n");
  printf("  -f, --jit-fpu              Enable FPU JIT backend\n");
  printf("  -m, --map <args...>        Map entry (same syntax as .cfg map line)\n");
  printf("  -M, --mouse <file> <key> [autoconnect]\n");
  printf("                             Mouse forwarding (toggle key, optional autoconnect)\n");
  printf("  -K, --keyboard <key> [grab] [autoconnect]\n");
  printf("                             Keyboard forwarding (optional grab/autoconnect)\n");
  printf("  -k, --kbfile <path>        Keyboard event source path\n");
  printf("  -p, --platform <name> [sub]\n");
  printf("                             Platform selection\n");
  printf("  -sv, --setvar <var> <val>  Platform setvar (single-token values only)\n");
  printf("\n");
  printf("Notes:\n");
  printf("  - For complex setvar or multi-arg values, use a .cfg file.\n");
  printf("  - You can also set %s and %s environment variables for the same specs.\n",
         PI_AFFINITY_ENV, PI_RT_ENV);
  printf("  - Set PISTORM_SYSLOG=1 to enable syslog logging for services.\n");
  printf("  - input=... acts as a fallback for keyboard/mouse if those are not set.\n");
  printf("  - RT priorities require CAP_SYS_NICE or a non-zero RLIMIT_RTPRIO.\n");
  printf("  - FC: first few transitions are logged at info when enabled; use --log-level debug for ongoing.\n");
  printf("  - Queue: set PISTORM_ENABLE_QUEUE=1 to enable batching (optional PISTORM_BATCH_BITS).\n");
}
