// SPDX-License-Identifier: MIT

#include <exec/libraries.h>
#include <exec/resident.h>
#include <exec/types.h>

#include <proto/exec.h>

#include "armaccel_library.h"

#define STR(s) #s
#define XSTR(s) STR(s)

#define LIBRARY_NAME "armaccel.library"
#define LIBRARY_DATE "(08 Mar 2026)"
#define LIBRARY_ID_STRING "armaccel.library 1.0 " LIBRARY_DATE
#define LIBRARY_VERSION 1
#define LIBRARY_REVISION 0
#define LIBRARY_PRIORITY 0

struct ExecBase *SysBase = NULL;
static BPTR g_seglist = 0;

int __attribute__((no_reorder)) _start(void) {
  return -1;
}

void _exit(int rc) {
  (void)rc;
  for (;;) {
  }
}

asm("romtag:                                \n"
    "       dc.w    " XSTR(RTC_MATCHWORD) "   \n"
    "       dc.l    romtag                  \n"
    "       dc.l    endcode                 \n"
    "       dc.b    " XSTR(RTF_AUTOINIT) "    \n"
    "       dc.b    " XSTR(LIBRARY_VERSION) " \n"
    "       dc.b    " XSTR(NT_LIBRARY) "      \n"
    "       dc.b    " XSTR(LIBRARY_PRIORITY) " \n"
    "       dc.l    _armaccel_library_name   \n"
    "       dc.l    _armaccel_library_id     \n"
    "       dc.l    _armaccel_lib_auto_init_tables\n"
    "endcode:\n");

char armaccel_library_name[] = LIBRARY_NAME;
char armaccel_library_id[] = LIBRARY_ID_STRING;

static struct Library * __attribute__((used))
init_library(struct Library *lib asm("d0"), BPTR seglist asm("a0")) {
  SysBase = *(struct ExecBase **)4L;
  g_seglist = seglist;

  lib->lib_Node.ln_Type = NT_LIBRARY;
  lib->lib_Node.ln_Name = armaccel_library_name;
  lib->lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
  lib->lib_Version = LIBRARY_VERSION;
  lib->lib_Revision = LIBRARY_REVISION;
  lib->lib_IdString = armaccel_library_id;
  return lib;
}

static BPTR __attribute__((used))
expunge_library(struct Library *lib asm("a6")) {
  BPTR seglist = 0;

  if (lib->lib_OpenCnt != 0) {
    lib->lib_Flags |= LIBF_DELEXP;
    return 0;
  }

  seglist = g_seglist;
  g_seglist = 0;
  Remove(&lib->lib_Node);
  FreeMem((UBYTE *)lib - lib->lib_NegSize, lib->lib_NegSize + lib->lib_PosSize);
  return seglist;
}

static struct Library * __attribute__((used))
open_library(struct Library *lib asm("a6"), ULONG version asm("d0")) {
  (void)version;
  lib->lib_OpenCnt++;
  lib->lib_Flags &= (UBYTE)~LIBF_DELEXP;
  return lib;
}

static BPTR __attribute__((used))
close_library(struct Library *lib asm("a6")) {
  if (lib->lib_OpenCnt > 0) {
    lib->lib_OpenCnt--;
  }
  if ((lib->lib_OpenCnt == 0) && ((lib->lib_Flags & LIBF_DELEXP) != 0)) {
    return expunge_library(lib);
  }
  return 0;
}

static ULONG __attribute__((used))
null_library(void) {
  return 0;
}

static ULONG armaccel_library_vectors[] = {
    (ULONG)open_library,
    (ULONG)close_library,
    (ULONG)expunge_library,
    (ULONG)null_library,
    (ULONG)ARMACCEL_IsSupportedELF,
    (ULONG)ARMACCEL_QueryELF,
    (ULONG)ARMACCEL_ExecuteELF,
    (ULONG)-1,
};

ULONG armaccel_lib_auto_init_tables[] = {
    sizeof(struct Library),
    (ULONG)armaccel_library_vectors,
    0,
    (ULONG)init_library,
};
