// SPDX-License-Identifier: MIT

#include <exec/types.h>
#include <exec/libraries.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include <dos/dos.h>
#include <stdio.h>

#include <workbench/startup.h>

#include "armaccel_library.h"

#define STR2(s) #s
#define STR(s) STR2(s)

#define ARMACCEL_LIB_NAME "armaccel.library"
#define ARMACCEL_LIB_VERSION 1

#define LVO_ARMACCEL_ISSUPPORTED (-30)
#define LVO_ARMACCEL_QUERY       (-36)
#define LVO_ARMACCEL_EXECUTE     (-42)

static void print_usage(const char *prog) {
  printf("Usage: %s <payload1.elf> [payload2.elf ...]\n", prog);
  printf("Executes ARMAccel ELF payloads through armaccel.library.\n");
}

static LONG lib_query_elf(struct Library *base, const char *path, struct ArmAccelELFInfo *info) {
  LONG ret;

  __asm volatile("movel %2,sp@-\n\t"
                 "movel %1,sp@-\n\t"
                 "movel %3,a6\n\t"
                 "jsr a6@(" STR(LVO_ARMACCEL_QUERY) ":W)\n\t"
                 "addw #8,sp\n\t"
                 "movel d0,%0"
                 : "=r"(ret)
                 : "r"(path), "r"(info), "r"(base)
                 : "a6", "d0", "memory");

  return ret;
}

static LONG lib_execute_elf(struct Library *base, const char *path, const struct ArmAccelRunOpts *opts,
                            struct ArmAccelResult *result) {
  LONG ret;

  __asm volatile("movel %4,sp@-\n\t"
                 "movel %3,sp@-\n\t"
                 "movel %2,sp@-\n\t"
                 "movel %1,a6\n\t"
                 "jsr a6@(" STR(LVO_ARMACCEL_EXECUTE) ":W)\n\t"
                 "addw #12,sp\n\t"
                 "movel d0,%0"
                 : "=r"(ret)
                 : "r"(base), "r"(path), "r"(opts), "r"(result)
                 : "a6", "d0", "memory");

  return ret;
}

static int run_one_payload(struct Library *lib_base, const char *path) {
  struct ArmAccelELFInfo info;
  struct ArmAccelResult result;
  LONG qrc;
  LONG erc;

  qrc = lib_query_elf(lib_base, path, &info);
  if (qrc == (LONG)ARMACCEL_ELF_COMPAT_INVALID_FILE) {
    printf("%s: not a valid ELF file\n", path);
    return 1;
  }
  if (qrc == (LONG)ARMACCEL_ELF_COMPAT_NOT_ARMACCEL) {
    printf("%s: not an ARMAccel personality ELF\n", path);
    return 1;
  }
  if (qrc == (LONG)ARMACCEL_ELF_COMPAT_WRONG_ABI) {
    printf("%s: wrong ARMAccel ABI version (need %u, got %u)\n", path,
           (unsigned int)ARMACCEL_LIBRARY_SUPPORTED_ABI_MAJOR, (unsigned int)info.abi_major);
    return 1;
  }
  if (qrc == (LONG)ARMACCEL_ELF_COMPAT_NEEDS_SERVICES) {
    printf("%s: requires unavailable services mask=$%08X\n", path,
           (unsigned int)(info.required_services & ~info.available_services));
    return 1;
  }

  erc = lib_execute_elf(lib_base, path, NULL, &result);
  if (erc != ARMACCEL_EXEC_OK) {
    printf("%s: execute failed rc=%d\n", path, (int)erc);
    return 2;
  }

  printf("%s: ok result0=$%08X result1=$%08X job_state=%u job_result=%u retval=$%08X%08X\n", path,
         (unsigned int)result.device_result0, (unsigned int)result.device_result1,
         (unsigned int)result.job_state, (unsigned int)result.job_result,
         (unsigned int)result.retval_hi, (unsigned int)result.retval_lo);
  return 0;
}

static int run_workbench_payloads(struct Library *lib_base, char **argv) {
  struct WBStartup *wbmsg;
  LONG i;
  int failures;

  wbmsg = (struct WBStartup *)argv;
  if ((wbmsg == NULL) || (wbmsg->sm_NumArgs < 2) || (wbmsg->sm_ArgList == NULL)) {
    return 5;
  }

  failures = 0;
  for (i = 1; i < wbmsg->sm_NumArgs; i++) {
    struct WBArg *arg = &wbmsg->sm_ArgList[i];
    BPTR old_dir = (BPTR)0;

    if (arg->wa_Lock != (BPTR)0) {
      old_dir = CurrentDir(arg->wa_Lock);
    }

    if ((arg->wa_Name != NULL) && (arg->wa_Name[0] != '\0')) {
      if (run_one_payload(lib_base, (const char *)arg->wa_Name) != 0) {
        failures++;
      }
    } else {
      failures++;
    }

    if (arg->wa_Lock != (BPTR)0) {
      CurrentDir(old_dir);
    }
  }

  return (failures == 0) ? 0 : 10;
}

int main(int argc, char **argv) {
  struct Library *lib_base;
  int i;
  int failures;

  lib_base = OpenLibrary((STRPTR)ARMACCEL_LIB_NAME, ARMACCEL_LIB_VERSION);
  if (lib_base == NULL) {
    printf("Failed to open %s v%u\n", ARMACCEL_LIB_NAME, (unsigned int)ARMACCEL_LIB_VERSION);
    return 20;
  }

  if (argc == 0) {
    int rc = run_workbench_payloads(lib_base, argv);
    CloseLibrary(lib_base);
    return rc;
  }

  if (argc < 2) {
    print_usage(argv[0]);
    CloseLibrary(lib_base);
    return 5;
  }

  failures = 0;
  for (i = 1; i < argc; i++) {
    if (run_one_payload(lib_base, argv[i]) != 0) {
      failures++;
    }
  }

  CloseLibrary(lib_base);
  return (failures == 0) ? 0 : 10;
}
