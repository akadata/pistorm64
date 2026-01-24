// SPDX-License-Identifier: MIT

#include "m68k.h"
#include "emulator.h"
#include "platforms/platforms.h"
#include "input/input.h"
#include "m68kcpu.h"

#include "platforms/amiga/Gayle.h"
#include "platforms/amiga/amiga-registers.h"
#include "platforms/amiga/amiga-interrupts.h"
#include "platforms/amiga/rtg/rtg.h"
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
#include "cpu_backend.h"

#include <assert.h>
#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


#include "m68kops.h"

#define KEY_POLL_INTERVAL_MSEC 1000

unsigned int ovl;

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

#define CORE_AUTO -1
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
extern int m68ki_initial_cycles;
extern int m68ki_remaining_cycles;

#define M68K_SET_IRQ(i)                                                                            \
  old_level = CPU_INT_LEVEL;                                                                       \
  CPU_INT_LEVEL = ((unsigned int)(i) << 8);                                                        \
  if (old_level != 0x0700 && CPU_INT_LEVEL == 0x0700)                                              \
    m68ki_cpu.nmi_pending = TRUE;
#define M68K_END_TIMESLICE                                                                         \
  m68ki_initial_cycles = GET_CYCLES();                                                             \
  SET_CYCLES(0);
#else
#define M68K_SET_IRQ m68k_set_irq
#define M68K_END_TIMESLICE m68k_end_timeslice()
#endif

#define NOP1()  __asm__ __volatile__("nop" ::: "memory")
#define NOP 	do { NOP1(); NOP1(); NOP1(); NOP1(); } while (0)

#define DEBUG_EMULATOR
#ifdef DEBUG_EMULATOR
#define DEBUG printf
#else
#define DEBUG(...)
#endif

// Configurable emulator options
unsigned int cpu_type = M68K_CPU_TYPE_68000;
unsigned int loop_cycles = 300;
static unsigned int ipl_nop_count = 8;
static const unsigned int ipl_nop_count_default = 8;
unsigned int irq_status = 0;

static const unsigned int loop_cycles_cap = 10000; // cap slices to keep service latency reasonable
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
  } else if (loop_cycles > 300) {
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
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
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
  m68ki_initial_cycles = num_cycles;

  /* See if interrupts came in */
  m68ki_check_interrupts(state);

  /* Make sure we're not stopped */
  if (!CPU_STOPPED) {
    /* Return point if we had an address error */
    m68ki_set_address_error_trap(state); /* auto-disable ( see m68kcpu.h ) */

#ifdef M68K_BUSERR_THING
    m68ki_check_bus_error_trap();
#endif

    /* Main loop.  Keep going until we run out of clock cycles */
    do {
      /* Set tracing according to T1. ( T0 is done inside instruction ) */
      m68ki_trace_t1(); /* auto-disable ( see m68kcpu.h ) */

      /* Set the address space for reads */
      m68ki_use_data_space(); /* auto-disable ( see m68kcpu.h ) */

      /* Call external hook to peek at CPU */
      m68ki_instr_hook(REG_PC); /* auto-disable ( see m68kcpu.h ) */

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
  return;
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
  state->gpio = gpio;
  m68k_pulse_reset(state);
  apply_affinity_from_env("cpu", CORE_CPU);
  apply_realtime_from_env("cpu", RT_DEFAULT_CPU);

cpu_loop:
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
        mouse_dx = mouse_dy = mouse_buttons = mouse_extra = 0;
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
    } else if (strcmp(argv[g], "--log-level") == 0 || strcmp(argv[g], "-l") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no log level specified.\n", argv[g]);
      } else {
        int level = log_parse_level(argv[++g]);
        if (level < 0) {
          printf("Invalid log level %s ( use error|warn|info|debug ).\n", argv[g]);
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
  srand((unsigned int)clock());

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

  signal(SIGINT, sigint_handler);

  amiga_reset_and_wait("pre-cpu");

  m68k_init();
  printf("Setting CPU type to %d.\n", cpu_type);
  m68k_set_cpu_type(&m68ki_cpu, cpu_type);
  cpu_pulse_reset();

  pthread_t ipl_tid = 0, cpu_tid, kbd_tid, mouse_tid = 0;
  int err;
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
  pthread_join(cpu_tid, NULL);

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

  if (kbd_tid) {
    pthread_join(kbd_tid, NULL);
  }
  if (mouse_tid) {
    pthread_join(mouse_tid, NULL);
  }
  if (ipl_tid) {
    pthread_join(ipl_tid, NULL);
  }

  if (cfg->platform->shutdown) {
    cfg->platform->shutdown(cfg);
  }

  #ifdef PS_PROTOCOL_HAS_CLEANUP
#ifdef PS_PROTOCOL_HAS_CLEANUP
  ps_cleanup_protocol();
#endif
  #endif

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
      if (mouse_hook_enabled && (mouse_buttons & 0x01)) {
        rres = (uint32_t)ps_read(type, addr);
        *res = (rres ^ 0x40);
        return 1;
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
        unsigned short result = (unsigned short)ps_read(type, addr);
        // bit 1 rmb, bit 2 mmb
        if (mouse_buttons & 0x06) {
          *res = (unsigned int)((result ^ ((mouse_buttons & 0x02) << 9))     // move rmb to bit 10
                                & (result ^ ((mouse_buttons & 0x04) << 6))); // move mmb to bit 8
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
      if (addr >= PIGFX_RTG_BASE && addr < PIGFX_UPPER) {
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
  if (platform_read_check(OP_TYPE_BYTE, address, &platform_res)) {
    return platform_res;
  }

  if (address & 0xFF000000) {
    return 0;
  }

  return (unsigned int)ps_read_8((uint32_t)address);
}

unsigned int m68k_read_memory_16(unsigned int address) {
  if (platform_read_check(OP_TYPE_WORD, address, &platform_res)) {
    return platform_res;
  }

  if (address & 0xFF000000) {
    return 0;
  }

  if (address & 0x01) {
    return ((unsigned int)(ps_read_8(address) << 8) | (unsigned int)ps_read_8(address + 1));
  }
  return (unsigned int)ps_read_16((uint32_t)address);
}

unsigned int m68k_read_memory_32(unsigned int address) {
  if (platform_read_check(OP_TYPE_LONGWORD, address, &platform_res)) {
    return platform_res;
  }

  if (address & 0xFF000000) {
    return 0;
  }

  if (address & 0x01) {
    return (unsigned int)ps_read(OP_TYPE_LONGWORD, address);
  }
  return (unsigned int)ps_read(OP_TYPE_LONGWORD, address);
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
        printf("OVL:%x\n", ovl);
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
      if (addr >= PIGFX_RTG_BASE && addr < PIGFX_UPPER) {
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
  if (platform_write_check(OP_TYPE_BYTE, address, value)) {
    return;
  }

  if (address & 0xFF000000) {
    return;
  }

  ps_write_8((uint32_t)address, (uint8_t)value);
  return;
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
  if (platform_write_check(OP_TYPE_WORD, address, value)) {
    return;
  }

  if (address & 0xFF000000) {
    return;
  }

  if (address & 0x01) {
    ps_write_8((uint32_t)address, (uint8_t)(value & 0xFF));
    ps_write_8((uint32_t)address + 1, (uint8_t)((value >> 8) & 0xFF));
    return;
  }

  ps_write_16((uint32_t)address, (uint16_t)value);
  return;
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
  if (platform_write_check(OP_TYPE_LONGWORD, address, value)) {
    return;
  }

  if (address & 0xFF000000) {
    return;
  }

  if (address & 0x01) {
    ps_write(OP_TYPE_LONGWORD, address, (uint32_t)value);
    return;
  }

  ps_write(OP_TYPE_LONGWORD, address, (uint32_t)value);
  return;
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
  printf("  -l, --log-level <level>    Set log level (error|warn|info|debug)\n");
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
  printf("  - input=... acts as a fallback for keyboard/mouse if those are not set.\n");
  printf("  - RT priorities require CAP_SYS_NICE or a non-zero RLIMIT_RTPRIO.\n");
}
