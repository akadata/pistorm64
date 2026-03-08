// SPDX-License-Identifier: MIT

#include "m68k.h"
#include "platforms/platforms.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <endian.h>
#include <sys/mman.h>

#include "rominfo.h"
#include "gpio/ps_protocol.h"

#define M68K_CPU_TYPES M68K_CPU_TYPE_SCC68070
#define PI_AFFINITY_ENV "PISTORM_AFFINITY"
#define PI_RT_ENV "PISTORM_RT"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

const char* cpu_types[M68K_CPU_TYPES] = {
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

const char* map_type_names[MAPTYPE_NUM] = {
    "NONE", 
    "rom", 
    "ram", 
    "register", 
    "ram_noalloc", 
    "wtcram",
    "image",  /* cryptodad */
};

const char* config_item_names[CONFITEM_NUM] = {
    "NONE",     
    "cpu",      
    "map",      
    "loopcycles", 
    "jit",    
    "jitfpu",
    "mouse",   
    "keyboard", 
    "platform", 
    "pistorm",
    "pistorm-gpclk-src",
    "pistorm-gpclk-div",
    "pistorm-mmio-wr-stretch",
    "pistorm-mmio-rd-stretch",
    "setvar",     
    "kbfile", 
    "affinity",
    "rtprio",
};

const char* mapcmd_names[MAPCMD_NUM] = {
    "NONE", 
    "type", 
    "address", 
    "size",          
    "range",
    "file", 
    "ovl",  
    "id",      
    "autodump_file", 
    "autodump_mem",
};

static size_t cfg_align_to_page(size_t size) {
  long page_size = sysconf(_SC_PAGESIZE);
  size_t page = (page_size > 0) ? (size_t)page_size : 4096u;
  return (size + page - 1u) & ~(page - 1u);
}

static void* cfg_try_mmap_low4g(size_t size) {
#if UINTPTR_MAX <= 0xFFFFFFFFu
  (void)size;
  return NULL;
#else
  const uintptr_t low_start = 0x10000000ull;
  const uintptr_t low_end = 0xF0000000ull;
  const uintptr_t step = 0x02000000ull; // 32MB granularity.
  const uintptr_t max_addr = 0xFFFFFFFFull;
  size_t aligned = cfg_align_to_page(size);

  if (aligned == 0 || aligned > (size_t)(low_end - low_start)) {
    return NULL;
  }

#ifdef MAP_FIXED_NOREPLACE
  for (uintptr_t base = low_start; base + aligned <= low_end; base += step) {
    void* p = mmap((void*)base, aligned, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) {
      continue;
    }
    if (((uintptr_t)p + aligned - 1u) <= max_addr) {
      return p;
    }
    munmap(p, aligned);
  }
#endif

  void* p = mmap(NULL, aligned, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) {
    return NULL;
  }
  if (((uintptr_t)p + aligned - 1u) <= max_addr) {
    return p;
  }
  munmap(p, aligned);
  return NULL;
#endif
}

unsigned char* cfg_alloc_mapped_data(size_t size, int zero_init, unsigned char* alloc_kind,
                                     const char* owner) {
  if (alloc_kind) {
    *alloc_kind = MAPALLOC_NONE;
  }
  if (!size) {
    return NULL;
  }

  const char* name = owner ? owner : "map";
  unsigned char* ptr = NULL;
  void* low4g = cfg_try_mmap_low4g(size);
  if (low4g) {
    ptr = (unsigned char*)low4g;
    if (zero_init) {
      memset(ptr, 0x00, size);
    }
    if (alloc_kind) {
      *alloc_kind = MAPALLOC_MMAP_LOW4G;
    }
    return ptr;
  }

  ptr = zero_init ? (unsigned char*)calloc(1, size) : (unsigned char*)malloc(size);
  if (!ptr) {
    return NULL;
  }
  if (zero_init) {
    // calloc already zeroes, keep explicit behavior in case allocator changes.
    memset(ptr, 0x00, size);
  }
  if (alloc_kind) {
    *alloc_kind = MAPALLOC_HEAP;
  }
  if (sizeof(void*) > 4 && (((uintptr_t)ptr) >> 32) != 0) {
    printf("[CFG] Warning: %s allocation not in low 4GB: %p\n", name, (void*)ptr);
  }
  return ptr;
}

void cfg_free_mapped_data(unsigned char* ptr, size_t size, unsigned char alloc_kind) {
  if (!ptr) {
    return;
  }

  switch (alloc_kind) {
  case MAPALLOC_MMAP_LOW4G: {
    size_t aligned = cfg_align_to_page(size);
    if (aligned) {
      munmap((void*)ptr, aligned);
    }
    break;
  }
  case MAPALLOC_HEAP:
    free(ptr);
    break;
  case MAPALLOC_EXTERNAL:
  case MAPALLOC_NONE:
  default:
    break;
  }
}

void cfg_set_map_data_allocation(struct emulator_config* cfg, int index, unsigned char* ptr,
                                 size_t alloc_size, unsigned char alloc_kind) {
  if (!cfg || index < 0 || index >= MAX_NUM_MAPPED_ITEMS) {
    return;
  }
  cfg->map_data[index] = ptr;
  cfg->map_alloc_size[index] = alloc_size;
  cfg->map_alloc_kind[index] = alloc_kind;
}

void cfg_release_map_data(struct emulator_config* cfg, int index) {
  if (!cfg || index < 0 || index >= MAX_NUM_MAPPED_ITEMS) {
    return;
  }
  if (cfg->map_data[index]) {
    cfg_free_mapped_data(cfg->map_data[index], cfg->map_alloc_size[index], cfg->map_alloc_kind[index]);
  }
  cfg->map_data[index] = NULL;
  cfg->map_alloc_size[index] = 0;
  cfg->map_alloc_kind[index] = MAPALLOC_NONE;
}

static int cfg_alloc_and_assign_map_slot(struct emulator_config* cfg, int index, size_t size,
                                         const char* owner) {
  unsigned char alloc_kind = MAPALLOC_NONE;
  unsigned char* map_ptr = cfg_alloc_mapped_data(size, 1, &alloc_kind, owner);
  if (!map_ptr) {
    return 0;
  }
  cfg_set_map_data_allocation(cfg, index, map_ptr, size, alloc_kind);
  return 1;
}

static int get_config_item_type(char* cmd) {
  if (strcasecmp(cmd, "rt-prio") == 0) {
    return CONFITEM_RTPRIO;
  }

  for (int i = 0; i < CONFITEM_NUM; i++) {
    if (strcasecmp(cmd, config_item_names[i]) == 0) {
      return i;
    }
  }

  return CONFITEM_NONE;
}

char *uppercase ( char *str )
{
  for ( int n = 0; n < strlen ( str ); n++ )
  {
    str [n] = toupper ( str [n] );
  }

  return str;
}


unsigned int get_m68k_cpu_type(const char* name) {
  if (!name) {
    printf("[CFG] Invalid CPU type (null) specified, defaulting to 68000.\n");
    return M68K_CPU_TYPE_68000;
  }

  // Accept common shorthand aliases seen in existing configs/tools.
  if (strcasecmp(name, "680ec20") == 0)
    name = "68EC020";
  else if (strcasecmp(name, "680ec30") == 0)
    name = "68EC030";
  else if (strcasecmp(name, "680ec40") == 0)
    name = "68EC040";
  else if (strcasecmp(name, "68060") == 0 || strcasecmp(name, "68EC060") == 0 ||
           strcasecmp(name, "68LC060") == 0) {
    // Current core set tops out at 68040 tables; keep user intent ("high-end CPU")
    // without silently dropping all the way to 68000.
    printf("[CFG] CPU type %s requested; mapping to 68040 (68060 tables not present).\n", name);
    name = "68040";
  }

  for (int i = 0; i < M68K_CPU_TYPES; i++) {
    if (strcasecmp(name, cpu_types[i]) == 0) {
      printf("[CFG] Set CPU type to %s.\n", cpu_types[i]);
      return (unsigned int)(i + 1);
    }
  }

  printf("[CFG] Invalid CPU type %s specified, defaulting to 68000.\n", name);
  return M68K_CPU_TYPE_68000;
}

static unsigned int get_map_cmd(char* name) {
  for (int i = 1; i < MAPCMD_NUM; i++) {
    if (strcmp(name, mapcmd_names[i]) == 0) {
      return (unsigned int)i;
    }
  }

  return MAPCMD_UNKNOWN;
}

static unsigned int get_map_type(char* name) {
  for (int i = 1; i < MAPTYPE_NUM; i++) {
    if (strcmp(name, map_type_names[i]) == 0) {
      return (unsigned int)i;
    }
  }

  return MAPTYPE_NONE;
}

static void trim_whitespace(char* str) {
  while (strlen(str) != 0 && (str[strlen(str) - 1] == ' ' || str[strlen(str) - 1] == '\t' ||
                              str[strlen(str) - 1] == 0x0A || str[strlen(str) - 1] == 0x0D)) {
    str[strlen(str) - 1] = '\0';
  }
}

unsigned int get_int(const char* str) {
  if (strlen(str) == 0)
    return (unsigned int)-1;

  unsigned int ret_int = 0;

  if (strlen(str) > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
    for (int i = 2; i < (int)strlen(str); i++) {
      char c = str[i];
      if (c >= '0' && c <= '9') {
        ret_int = (unsigned int)(c - '0') | (ret_int << 4);
      } else {
        switch (c) {
        case 'A':
        case 'a':
          ret_int = 0xA | (ret_int << 4);
          break;
        case 'B':
        case 'b':
          ret_int = 0xB | (ret_int << 4);
          break;
        case 'C':
        case 'c':
          ret_int = 0xC | (ret_int << 4);
          break;
        case 'D':
        case 'd':
          ret_int = 0xD | (ret_int << 4);
          break;
        case 'E':
        case 'e':
          ret_int = 0xE | (ret_int << 4);
          break;
        case 'F':
        case 'f':
          ret_int = 0xF | (ret_int << 4);
          break;
        case 'K':
        case 'k':
          ret_int = ret_int * SIZE_KILO;
          break;
        case 'M':
        case 'm':
          ret_int = ret_int * SIZE_MEGA;
          break;
        case 'G':
        case 'g':
          ret_int = ret_int * SIZE_GIGA;
          break;
        default:
          printf("[CFG] Unknown character %c in hex value.\n", str[i]);
          break;
        }
      }
    }
    return ret_int;
  } else {
    ret_int = (unsigned int)strtoul(str, NULL, 10);
    char last = str[strlen(str) - 1];
    if (last == 'K' || last == 'k')
      ret_int = ret_int * SIZE_KILO;
    else if (last == 'M' || last == 'm')
      ret_int = ret_int * SIZE_MEGA;
    else if (last == 'G' || last == 'g')
      ret_int = ret_int * SIZE_GIGA;

    return ret_int;
  }
}

static void get_next_string(char* str, char* str_out, int* strpos, char separator) {
  int str_pos = 0, out_pos = 0, startquote = 0, endstring = 0;

  if (!str_out)
    return;

  if (strpos)
    str_pos = *strpos;

  while ((str[str_pos] == ' ' || str[str_pos] == '\t') && str_pos < (int)strlen(str)) {
    str_pos++;
  }

  if (str[str_pos] == '\"') {
    str_pos++;
    startquote = 1;
  }

  for (int i = str_pos; i < (int)strlen(str); i++) {
    str_out[out_pos] = str[i];

    if (startquote) {
      if (str[i] == '\"')
        endstring = 1;
    } else {
      if ((separator == ' ' && (str[i] == ' ' || str[i] == '\t')) || str[i] == separator) {
        endstring = 1;
      }
    }

    if (endstring) {
      str_out[out_pos] = '\0';
      if (strpos) {
        *strpos = i + 1;
      }
      break;
    }

    out_pos++;
    if (i + 1 == (int)strlen(str) && strpos) {
      *strpos = i + 1;
      str_out[out_pos] = '\0';
    }
  }
}

int apply_config_line(struct emulator_config* cfg, const char* line, int line_no) {
  char parse_line[512];
  char cur_cmd[128];
  int str_pos = 0;
  int report_line = line_no > 0 ? line_no : 0;

  if (!cfg || !line)
    return -1;

  memset(parse_line, 0x00, sizeof(parse_line));
  strncpy(parse_line, line, sizeof(parse_line) - 1);

  if (strlen(parse_line) <= 2 || parse_line[0] == '#' || parse_line[0] == '/')
    return 0;

  trim_whitespace(parse_line);
  if (!strlen(parse_line))
    return 0;

  get_next_string(parse_line, cur_cmd, &str_pos, ' ');

  switch (get_config_item_type(cur_cmd)) {
  case CONFITEM_CPUTYPE:
    cfg->cpu_type = get_m68k_cpu_type(parse_line + str_pos);
    break;
  case CONFITEM_MAP: {
    unsigned int maptype = 0, mapsize = 0, mapaddr = 0, autodump = 0;
    unsigned int mirraddr = ((unsigned int)-1);
    char mapfile[128], mapid[128];
    memset(mapfile, 0x00, sizeof(mapfile));
    memset(mapid, 0x00, sizeof(mapid));

    while (str_pos < (int)strlen(parse_line)) {
      get_next_string(parse_line, cur_cmd, &str_pos, '=');
      switch (get_map_cmd(cur_cmd)) {
      case MAPCMD_TYPE:
        get_next_string(parse_line, cur_cmd, &str_pos, ' ');
        maptype = get_map_type(cur_cmd);
        break;
      case MAPCMD_ADDRESS:
        get_next_string(parse_line, cur_cmd, &str_pos, ' ');
        mapaddr = get_int(cur_cmd);
        break;
      case MAPCMD_SIZE:
        get_next_string(parse_line, cur_cmd, &str_pos, ' ');
        mapsize = get_int(cur_cmd);
        break;
      case MAPCMD_RANGE:
        get_next_string(parse_line, cur_cmd, &str_pos, '-');
        mapaddr = get_int(cur_cmd);
        get_next_string(parse_line, cur_cmd, &str_pos, ' ');
        mapsize = get_int(cur_cmd) - 1 - mapaddr;
        break;
      case MAPCMD_FILENAME:
        get_next_string(parse_line, cur_cmd, &str_pos, ' ');
        strncpy(mapfile, cur_cmd, sizeof(mapfile) - 1);
mapfile[sizeof(mapfile) - 1] = '\0';  // Ensure null termination
        break;
      case MAPCMD_MAP_ID:
        get_next_string(parse_line, cur_cmd, &str_pos, ' ');
        strncpy(mapid, cur_cmd, sizeof(mapid) - 1);
mapid[sizeof(mapid) - 1] = '\0';  // Ensure null termination
        break;
      case MAPCMD_OVL_REMAP:
        get_next_string(parse_line, cur_cmd, &str_pos, ' ');
        mirraddr = get_int(cur_cmd);
        break;
      case MAPCMD_AUTODUMP_FILE:
      case MAPCMD_AUTODUMP_MEM:
        autodump = get_map_cmd(cur_cmd);
        break;
      default:
        printf("[CFG] Unknown/unhandled map argument %s on line %d.\n", cur_cmd, report_line);
        break;
      }
    }
    char* map_backing = mapfile;
    if (maptype == MAPTYPE_RAM_NOALLOC) {
      if (mapfile[0] != '\0') {
        printf("[CFG] ram_noalloc ignores file= backing from config; runtime code must bind map_data.\n");
      }
      map_backing = NULL;
    }
    add_mapping(cfg, maptype, mapaddr, mapsize, mirraddr, map_backing, mapid, autodump);

    break;
  }
  case CONFITEM_LOOPCYCLES:
    cfg->loop_cycles = get_int(parse_line + str_pos);
    printf("[CFG] Set CPU loop cycles to %d.\n", cfg->loop_cycles);
    break;
  case CONFITEM_JIT: {
    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    unsigned char enable = 0;
    if (strlen(cur_cmd)) {
      if (!strcasecmp(cur_cmd, "1") || !strcasecmp(cur_cmd, "on") || !strcasecmp(cur_cmd, "yes") ||
          !strcasecmp(cur_cmd, "true")) {
        enable = 1;
      }
    }
    cfg->enable_jit = enable;
    printf("[CFG] JIT backend %s via config.\n", cfg->enable_jit ? "enabled" : "disabled");
    break;
  }
  case CONFITEM_JIT_FPU: {
    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    unsigned char enable = 0;
    if (strlen(cur_cmd)) {
      if (!strcasecmp(cur_cmd, "1") || !strcasecmp(cur_cmd, "on") || !strcasecmp(cur_cmd, "yes") ||
          !strcasecmp(cur_cmd, "true")) {
        enable = 1;
      }
    }
    cfg->enable_fpu_jit = enable;
    printf("[CFG] FPU JIT %s via config.\n", cfg->enable_fpu_jit ? "enabled" : "disabled");
    break;
  }
  case CONFITEM_MOUSE:
    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    cfg->mouse_file = (char*)calloc(1, strlen(cur_cmd) + 1);
    strcpy(cfg->mouse_file, cur_cmd);
    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    cfg->mouse_toggle_key = cur_cmd[0];
    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    cfg->mouse_autoconnect = (strcmp(cur_cmd, "autoconnect") == 0) ? 1 : 0;
    cfg->mouse_enabled = 1;
    printf("[CFG] Enabled mouse event forwarding from file %s, toggle key %c.\n", cfg->mouse_file,
           cfg->mouse_toggle_key);
    break;
  case CONFITEM_KEYBOARD:
    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    cfg->keyboard_toggle_key = cur_cmd[0];
    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    cfg->keyboard_grab = (strcmp(cur_cmd, "grab") == 0) ? 1 : 0;
    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    cfg->keyboard_autoconnect = (strcmp(cur_cmd, "autoconnect") == 0) ? 1 : 0;
    printf("[CFG] Enabled keyboard event forwarding, toggle key %c", cfg->keyboard_toggle_key);
    if (cfg->keyboard_grab)
      printf(", locking from host when connected");
    if (cfg->keyboard_autoconnect)
      printf(", connected to guest at startup");
    printf(".\n");
    break;
  case CONFITEM_KBFILE:
    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    cfg->keyboard_file = (char*)calloc(1, strlen(cur_cmd) + 1);
    strcpy(cfg->keyboard_file, cur_cmd);
    printf("[CFG] Set keyboard event source file to %s.\n", cfg->keyboard_file);
    break;
  case CONFITEM_AFFINITY:
    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    if (!strlen(cur_cmd)) {
      printf("[CFG] affinity command requires a value.\n");
      break;
    }
    setenv(PI_AFFINITY_ENV, cur_cmd, 1);
    printf("[CFG] Set thread affinity: %s.\n", cur_cmd);
    break;
  case CONFITEM_RTPRIO:
    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    if (!strlen(cur_cmd)) {
      printf("[CFG] rtprio command requires a value.\n");
      break;
    }
    setenv(PI_RT_ENV, cur_cmd, 1);
    printf("[CFG] Set RT priorities: %s.\n", cur_cmd);
    break;
  case CONFITEM_PLATFORM: {
    char platform_name[128], platform_sub[128];
    memset(platform_name, 0x00, sizeof(platform_name));
    memset(platform_sub, 0x00, sizeof(platform_sub));
    get_next_string(parse_line, platform_name, &str_pos, ' ');
    printf("[CFG] Setting platform to %s", platform_name);
    get_next_string(parse_line, platform_sub, &str_pos, ' ');
    if (strlen(platform_sub))
      printf(" (sub: %s)", platform_sub);
    printf("\n");
    cfg->platform = make_platform_config(platform_name, platform_sub);
    break;
  }
  case CONFITEM_PISTORM:
    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    if (!strlen(cur_cmd)) {
      printf("[CFG] pistorm command requires one argument: kernel|userspace.\n");
      break;
    }
    if (ps_select_backend(cur_cmd) == 0) {
      printf("[CFG] PiStorm backend selected: %s.\n", ps_get_backend());
    } else {
      printf("[CFG] Invalid or unavailable pistorm backend '%s'. Valid values: kernel|userspace.\n",
             cur_cmd);
    }
    break;
  case CONFITEM_PISTORM_GPCLK_SRC: {
    unsigned int src = 0;
    int ret;

    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    if (!strlen(cur_cmd)) {
      printf("[CFG] pistorm-gpclk-src command requires a value (0..15).\n");
      break;
    }

    src = get_int(cur_cmd);
    ret = ps_set_userspace_gpclk_src((uint32_t)src);
    if (ret == 0) {
      printf("[CFG] userspace GPCLK source set to %u.\n", src);
    } else if (ret == -EBUSY) {
      printf("[CFG] userspace GPCLK source ignored (backend already initialized).\n");
    } else {
      printf("[CFG] invalid pistorm-gpclk-src value '%s' (valid 0..15).\n", cur_cmd);
    }
    break;
  }
  case CONFITEM_PISTORM_GPCLK_DIV: {
    unsigned int div = 0;
    int ret;

    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    if (!strlen(cur_cmd)) {
      printf("[CFG] pistorm-gpclk-div command requires a value (1..4095).\n");
      break;
    }

    div = get_int(cur_cmd);
    ret = ps_set_userspace_gpclk_div((uint32_t)div);
    if (ret == 0) {
      printf("[CFG] userspace GPCLK divider set to %u.\n", div);
    } else if (ret == -EBUSY) {
      printf("[CFG] userspace GPCLK divider ignored (backend already initialized).\n");
    } else {
      printf("[CFG] invalid pistorm-gpclk-div value '%s' (valid 1..4095).\n", cur_cmd);
    }
    break;
  }
  case CONFITEM_PISTORM_MMIO_WR_STRETCH: {
    unsigned int count = 0;
    int ret;

    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    if (!strlen(cur_cmd)) {
      printf("[CFG] pistorm-mmio-wr-stretch command requires a value (1..64).\n");
      break;
    }

    count = get_int(cur_cmd);
    ret = ps_set_userspace_wr_stretch((uint32_t)count);
    if (ret == 0) {
      printf("[CFG] userspace WR strobe stretch set to %u.\n", count);
    } else if (ret == -EBUSY) {
      printf("[CFG] userspace WR strobe stretch ignored (backend already initialized).\n");
    } else {
      printf("[CFG] invalid pistorm-mmio-wr-stretch value '%s' (valid 1..64).\n", cur_cmd);
    }
    break;
  }
  case CONFITEM_PISTORM_MMIO_RD_STRETCH: {
    unsigned int count = 0;
    int ret;

    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    if (!strlen(cur_cmd)) {
      printf("[CFG] pistorm-mmio-rd-stretch command requires a value (1..64).\n");
      break;
    }

    count = get_int(cur_cmd);
    ret = ps_set_userspace_rd_stretch((uint32_t)count);
    if (ret == 0) {
      printf("[CFG] userspace RD strobe stretch set to %u.\n", count);
    } else if (ret == -EBUSY) {
      printf("[CFG] userspace RD strobe stretch ignored (backend already initialized).\n");
    } else {
      printf("[CFG] invalid pistorm-mmio-rd-stretch value '%s' (valid 1..64).\n", cur_cmd);
    }
    break;
  }
  case CONFITEM_SETVAR: {
    if (!cfg->platform) {
      printf("[CFG] Warning: setvar used in config file with no platform specified.\n");
      break;
    }

    char var_name[128], var_value[128];
    memset(var_name, 0x00, sizeof(var_name));
    memset(var_value, 0x00, sizeof(var_value));
    get_next_string(parse_line, var_name, &str_pos, ' ');
    get_next_string(parse_line, var_value, &str_pos, ' ');
    cfg->platform->setvar(cfg, var_name, var_value);
    break;
  }
  case CONFITEM_NONE:
  default:
    printf("[CFG] Unknown config item %s on line %d.\n", cur_cmd, report_line);
    break;
  }

  return 0;
}

int preparse_pistorm_backend(const char* filename) {
  FILE* in = NULL;
  char parse_line[512];
  char cur_cmd[128];
  int str_pos = 0;
  int found = 0;
  int line_no = 0;

  if (!filename || !filename[0]) {
    return -1;
  }

  in = fopen(filename, "rb");
  if (!in) {
    return -1;
  }

  while (fgets(parse_line, sizeof(parse_line), in)) {
    line_no++;
    str_pos = 0;
    memset(cur_cmd, 0x00, sizeof(cur_cmd));

    trim_whitespace(parse_line);
    if (strlen(parse_line) <= 2 || parse_line[0] == '#' || parse_line[0] == '/') {
      continue;
    }

    get_next_string(parse_line, cur_cmd, &str_pos, ' ');
    if (strcasecmp(cur_cmd, "pistorm") == 0) {
      memset(cur_cmd, 0x00, sizeof(cur_cmd));
      get_next_string(parse_line, cur_cmd, &str_pos, ' ');
      if (!strlen(cur_cmd)) {
        printf("[CFG] pistorm directive missing value on line %d in %s.\n", line_no, filename);
        continue;
      }

      if (ps_select_backend(cur_cmd) == 0) {
        found = 1;
      } else {
        printf("[CFG] invalid pistorm backend '%s' on line %d in %s.\n", cur_cmd, line_no, filename);
      }
      continue;
    }

    if (strcasecmp(cur_cmd, "pistorm-gpclk-src") == 0) {
      unsigned int src = 0;
      int ret;

      memset(cur_cmd, 0x00, sizeof(cur_cmd));
      get_next_string(parse_line, cur_cmd, &str_pos, ' ');
      if (!strlen(cur_cmd)) {
        printf("[CFG] pistorm-gpclk-src missing value on line %d in %s.\n", line_no, filename);
        continue;
      }

      src = get_int(cur_cmd);
      ret = ps_set_userspace_gpclk_src((uint32_t)src);
      if (ret < 0) {
        printf("[CFG] invalid pistorm-gpclk-src '%s' on line %d in %s.\n", cur_cmd, line_no, filename);
      }
      continue;
    }

    if (strcasecmp(cur_cmd, "pistorm-gpclk-div") == 0) {
      unsigned int div = 0;
      int ret;

      memset(cur_cmd, 0x00, sizeof(cur_cmd));
      get_next_string(parse_line, cur_cmd, &str_pos, ' ');
      if (!strlen(cur_cmd)) {
        printf("[CFG] pistorm-gpclk-div missing value on line %d in %s.\n", line_no, filename);
        continue;
      }

      div = get_int(cur_cmd);
      ret = ps_set_userspace_gpclk_div((uint32_t)div);
      if (ret < 0) {
        printf("[CFG] invalid pistorm-gpclk-div '%s' on line %d in %s.\n", cur_cmd, line_no, filename);
      }
      continue;
    }

    if (strcasecmp(cur_cmd, "pistorm-mmio-wr-stretch") == 0) {
      unsigned int count = 0;
      int ret;

      memset(cur_cmd, 0x00, sizeof(cur_cmd));
      get_next_string(parse_line, cur_cmd, &str_pos, ' ');
      if (!strlen(cur_cmd)) {
        printf("[CFG] pistorm-mmio-wr-stretch missing value on line %d in %s.\n", line_no, filename);
        continue;
      }

      count = get_int(cur_cmd);
      ret = ps_set_userspace_wr_stretch((uint32_t)count);
      if (ret < 0) {
        printf("[CFG] invalid pistorm-mmio-wr-stretch '%s' on line %d in %s.\n", cur_cmd, line_no,
               filename);
      }
      continue;
    }

    if (strcasecmp(cur_cmd, "pistorm-mmio-rd-stretch") == 0) {
      unsigned int count = 0;
      int ret;

      memset(cur_cmd, 0x00, sizeof(cur_cmd));
      get_next_string(parse_line, cur_cmd, &str_pos, ' ');
      if (!strlen(cur_cmd)) {
        printf("[CFG] pistorm-mmio-rd-stretch missing value on line %d in %s.\n", line_no, filename);
        continue;
      }

      count = get_int(cur_cmd);
      ret = ps_set_userspace_rd_stretch((uint32_t)count);
      if (ret < 0) {
        printf("[CFG] invalid pistorm-mmio-rd-stretch '%s' on line %d in %s.\n", cur_cmd, line_no,
               filename);
      }
    }
  }

  fclose(in);
  return found ? 0 : 1;
}

void add_mapping(struct emulator_config* cfg, unsigned int type, unsigned int addr,
                 unsigned int size, unsigned int mirr_addr, char* filename, const char* map_id,
                 unsigned int autodump) {
  unsigned int index = 0;
  long file_size = 0;
  FILE* in = NULL;

  while (index < MAX_NUM_MAPPED_ITEMS) {
    if (cfg->map_type[index] == MAPTYPE_NONE) {
      break;
    }
    index++;
  }
  if (index == MAX_NUM_MAPPED_ITEMS) {
    printf("[CFG] Unable to map item, only %d items can be mapped with current binary.\n", MAX_NUM_MAPPED_ITEMS);
    return;
  }

  cfg->map_type[index] = (unsigned char)type;
  cfg->map_offset[index] = addr;
  cfg->map_size[index] = size;
  cfg->map_high[index] = addr + size;
  cfg->map_mirror[index] = mirr_addr;
  cfg_set_map_data_allocation(cfg, (int)index, NULL, 0, MAPALLOC_NONE);
  if (strlen(map_id)) {
    if (cfg->map_id[index]) {
      free(cfg->map_id[index]);
    }
    cfg->map_id[index] = (char*)malloc(strlen(map_id) + 1);
    strcpy(cfg->map_id[index], map_id);
  }

  switch (type) {
  case MAPTYPE_RAM_NOALLOC:
    printf("[CFG] Adding %d byte (%d MB) RAM mapping %s...\n", size, size / 1024 / 1024, map_id);
    cfg_set_map_data_allocation(cfg, (int)index, (unsigned char*)filename, 0, MAPALLOC_EXTERNAL);
    break;
  case MAPTYPE_RAM_WTC:
    printf("[CFG] Allocating %d bytes for Write-Through Cached RAM mapping (%.1f MB)...\n", size,
           (float)size / 1024.0f / 1024.0f);
    if (!cfg_alloc_and_assign_map_slot(cfg, (int)index, size, map_id)) {
      printf("[CFG] ERROR: Unable to allocate memory for mapped RAM!\n");
      goto mapping_failed;
    }
    // This may look a bit weird, but it adds a read range for the WTC RAM. Writes still go
    // through to the mapped read/write functions.
    m68k_add_rom_range((uint32_t)cfg->map_offset[index], (uint32_t)cfg->map_high[index],
                       cfg->map_data[index]);
    break;
  case MAPTYPE_RAM:
    printf("[CFG] Allocating %d bytes for RAM mapping (%d MB)...\n", size, size / 1024 / 1024);
    if (!cfg_alloc_and_assign_map_slot(cfg, (int)index, size, map_id)) {
      printf("[CFG] ERROR: Unable to allocate memory for mapped RAM!\n");
      goto mapping_failed;
    }
    break;
  case MAPTYPE_ROM:
    in = fopen(filename, "rb");
    if (!in) {
      if (!autodump) {
        printf("[CFG] Failed to open file %s for ROM mapping. Using onboard ROM instead, if available.\n",
               filename);
        goto mapping_failed;
      } else if (autodump == MAPCMD_AUTODUMP_FILE) {
        printf("[CFG] Could not open file %s for ROM mapping. Autodump flag is set, dumping to file.\n",
               filename);
        dump_range_to_file((uint32_t)cfg->map_offset[index], cfg->map_size[index], filename);
        in = fopen(filename, "rb");
        if (in == NULL) {
          printf("[CFG] Could not open dumped file for reading. Using onboard ROM instead, if available.\n");
          goto mapping_failed;
        }
      } else if (autodump == MAPCMD_AUTODUMP_MEM) {
        printf("[CFG] Could not open file %s for ROM mapping. Autodump flag is set, dumping to memory.\n",
               filename);
        unsigned char map_alloc_kind = MAPALLOC_NONE;
        unsigned char* rom_buf = cfg_alloc_mapped_data(cfg->map_size[index], 1, &map_alloc_kind, map_id);
        if (rom_buf) {
          for (unsigned int off = 0; off < cfg->map_size[index]; off += 2) {
            uint16_t in16 = be16toh(read16((uint32_t)cfg->map_offset[index] + off));
            memcpy(&rom_buf[off], &in16, sizeof(in16));
          }
          cfg_set_map_data_allocation(cfg, (int)index, rom_buf, cfg->map_size[index], map_alloc_kind);
        }
        cfg->rom_size[index] = cfg->map_size[index];
        if (cfg->map_data[index] == NULL) {
          printf("[CFG] Could not dump range to memory. Using onboard ROM instead, if available.\n");
          goto mapping_failed;
        }
        goto skip_file_ops;
      }
    }
    fseek(in, 0, SEEK_END);
    file_size = ftell(in);
    if (file_size < 0) {
      printf("[CFG] Failed to determine file size for %s.\n", filename);
      goto mapping_failed;
    }
    if (size == 0) {
      cfg->map_size[index] = (unsigned int)file_size;
      cfg->map_high[index] = addr + cfg->map_size[index];
    }
    fseek(in, 0, SEEK_SET);
    unsigned char rom_alloc_kind = MAPALLOC_NONE;
    unsigned char* rom_buf = cfg_alloc_mapped_data(cfg->map_size[index], 1, &rom_alloc_kind, map_id);
    cfg_set_map_data_allocation(cfg, (int)index, rom_buf, cfg->map_size[index], rom_alloc_kind);
    cfg->rom_size[index] =
        (cfg->map_size[index] <= (unsigned long)file_size) ? cfg->map_size[index]
                                                           : (unsigned int)file_size;
    if (!cfg->map_data[index]) {
      printf("[CFG] ERROR: Unable to allocate memory for mapped ROM!\n");
      goto mapping_failed;
    }
    fread(cfg->map_data[index], cfg->rom_size[index], 1, in);
    if (in)
      fclose(in);
  skip_file_ops:
    displayRomInfo(cfg->map_data[index], cfg->rom_size[index]);
    if (cfg->map_size[index] == cfg->rom_size[index])
      m68k_add_rom_range((uint32_t)cfg->map_offset[index], (uint32_t)cfg->map_high[index],
                         cfg->map_data[index]);
    if (cfg->map_data[index] && cfg->map_size[index]) {
      if (mlock(cfg->map_data[index], cfg->map_size[index]) != 0) {
        printf("[CFG] Warning: mlock on ROM mapping failed (%s)\n", strerror(errno));
      }
      const char* rom_rw_env = getenv("PISTORM_JIT_ROM_WRITABLE");
      int keep_rom_writable = (rom_rw_env && atoi(rom_rw_env) != 0);
      long page_size = sysconf(_SC_PAGESIZE);
      if (page_size > 0) {
        uintptr_t base = (uintptr_t)cfg->map_data[index];
        uintptr_t aligned_base = base & ~((uintptr_t)page_size - 1u);
        size_t len = cfg->map_size[index] + (size_t)(base - aligned_base);
        len = (len + (size_t)page_size - 1u) & ~((size_t)page_size - 1u);
        if (keep_rom_writable) {
          printf("[CFG] ROM mapping left writable (PISTORM_JIT_ROM_WRITABLE=1).\n");
        } else {
          if (mprotect((void *)aligned_base, len, PROT_READ) != 0) {
            printf("[CFG] Warning: mprotect ROM mapping failed (%s)\n", strerror(errno));
          } else {
            printf("[CFG] ROM mapping set read-only (mprotect).\n");
          }
        }
      }
    }
    break;
  case MAPTYPE_REGISTER:
  default:
    break;
  }

  printf("[CFG] [MAP %d] Added %s mapping for range %.8lX-%.8lX ID: %s\n", index,
         map_type_names[type], cfg->map_offset[index], cfg->map_high[index] - 1,
         cfg->map_id[index] ? cfg->map_id[index] : "None");

  return;

mapping_failed:;
  cfg_release_map_data(cfg, (int)index);
  cfg->map_type[index] = MAPTYPE_NONE;
  if (in) {
    fclose(in);
  }
  cfg->ranges_dirty = 1;
}

void free_config_file(struct emulator_config* cfg) {
  if (!cfg) {
    printf("[CFG] Tried to free NULL config, aborting.\n");
    return;
  }

  if (cfg->platform) {
    cfg->platform->shutdown(cfg);
    free(cfg->platform);
    cfg->platform = NULL;
  }

  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    cfg_release_map_data(cfg, i);
    if (cfg->map_id[i]) {
      free(cfg->map_id[i]);
      cfg->map_id[i] = NULL;
    }
  }

  if (cfg->mouse_file) {
    free(cfg->mouse_file);
    cfg->mouse_file = NULL;
  }
  if (cfg->keyboard_file) {
    free(cfg->keyboard_file);
    cfg->keyboard_file = NULL;
  }

  m68k_clear_ranges();

  printf("[CFG] Config file freed. Maybe.\n");
}

struct emulator_config* load_config_file(const char* filename) {
  FILE* in = fopen(filename, "rb");
  if (in == NULL) {
    printf("[CFG] Failed to open config file %s for reading.\n", filename);
    return NULL;
  }

  char* parse_line = NULL;
  struct emulator_config* cfg = NULL;
  int cur_line = 1;

  parse_line = (char*)calloc(1, 512);
  if (!parse_line) {
    printf("[CFG] Failed to allocate memory for config file line buffer.\n");
    return NULL;
  }
  cfg = (struct emulator_config*)calloc(1, sizeof(struct emulator_config));
  if (!cfg) {
    printf("[CFG] Failed to allocate memory for temporary emulator config.\n");
    goto load_failed;
  }

  memset(cfg, 0x00, sizeof(struct emulator_config));
  cfg->cpu_type = M68K_CPU_TYPE_68000;

  while (!feof(in)) {
    memset(parse_line, 0x00, 512);
    fgets(parse_line, 512, in);
    apply_config_line(cfg, parse_line, cur_line);
    cur_line++;
  }
  goto load_successful;

load_failed:;
  if (cfg) {
    for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
      cfg_release_map_data(cfg, i);
    }
    free(cfg);
    cfg = NULL;
  }
load_successful:;
  if (parse_line)
    free(parse_line);

  return cfg;
}

int get_named_mapped_item(struct emulator_config* cfg, const char* name) {
  if (strlen(name) == 0)
    return -1;

  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE || !cfg->map_id[i])
      continue;
    if (strcmp(name, cfg->map_id[i]) == 0)
      return i;
  }

  return -1;
}

int get_mapped_item_by_address(struct emulator_config* cfg, uint32_t address) {
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE || !cfg->map_data[i])
      continue;
    else if (address >= cfg->map_offset[i] && address < cfg->map_high[i]) {
      if (cfg->map_type[i] == MAPTYPE_RAM || cfg->map_type[i] == MAPTYPE_RAM_NOALLOC ||
          cfg->map_type[i] == MAPTYPE_ROM)
        return i;
    }
  }

  return -1;
}

uint8_t* get_mapped_data_pointer_by_address(struct emulator_config* cfg, uint32_t address) {
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
    if (cfg->map_type[i] == MAPTYPE_NONE || !cfg->map_data[i])
      continue;
    else if (address >= cfg->map_offset[i] && address < cfg->map_high[i]) {
      if (cfg->map_type[i] == MAPTYPE_RAM || cfg->map_type[i] == MAPTYPE_RAM_NOALLOC ||
          cfg->map_type[i] == MAPTYPE_ROM)
        return cfg->map_data[i] + (address - cfg->map_offset[i]);
    }
  }

  return NULL;
}
