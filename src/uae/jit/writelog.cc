/*
 * UAE - The Un*x Amiga Emulator
 *
 * Standard write_log that writes to the console
 *
 * Copyright 2001 Bernd Schmidt
 */
#include "sysdeps.h"
#include "uae/uaestring.h"
#include "options.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define WRITE_LOG_BUF_SIZE 4096
FILE* debugfile = NULL;

int consoleopen = 0;
static int realconsole;
static TCHAR* console_buffer;

void console_out(const TCHAR* format, ...)
{
	va_list parms;
	TCHAR buffer[WRITE_LOG_BUF_SIZE];

	va_start(parms, format);
	_vsntprintf(buffer, WRITE_LOG_BUF_SIZE - 1, format, parms);
	va_end(parms);
	printf("%s", buffer);
}

void f_out(FILE* f, const TCHAR* format, ...)
{
	if (f == NULL)
	{
		return;
	}
	TCHAR buffer[WRITE_LOG_BUF_SIZE];
	va_list parms;
	va_start(parms, format);
	_vsntprintf(buffer, WRITE_LOG_BUF_SIZE - 1, format, parms);
	va_end(parms);
	printf("%s", buffer);
}

TCHAR console_getch(void)
{
	//flushmsgpump();
	if (console_buffer)
	{
		return 0;
	}
	if (realconsole)
	{
		// Avoid direct access to stdin to prevent PIC issues
		return 0; // Return 0 as a placeholder
	}
	if (consoleopen < 0)
	{
		unsigned long len;

		for (;;)
		{
			// Avoid direct access to stdin to prevent PIC issues
			const char out = 0; // Placeholder
			return out;
		}
	}
	return 0;
}

void jit_abort(const TCHAR* format, ...)
{
	static int happened;
	TCHAR buffer[WRITE_LOG_BUF_SIZE];
	va_list parms;
	va_start(parms, format);

	vsnprintf(buffer, WRITE_LOG_BUF_SIZE - 1, format, parms);
	write_log("%s", buffer);  // This will expand to z3660_printf due to the macro in sysdeps.h
	va_end(parms);
	if (!happened)
		printf("JIT: Serious error:\n%s", buffer);
	happened = 1;
	while(1)
	{
		// Exit instead of infinite loop to prevent hanging
		exit(1);
	}
}
