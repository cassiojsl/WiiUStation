/*
 * WiiUStation - Cafe OS codegen (JIT) area for Lightrec.
 *
 * The codegen area is the only memory Cafe OS lets an application execute
 * generated code from.  It is W^X: writable *or* executable.  Lightrec's
 * writes into it (see patches/0001-*.patch) are wrapped in
 * wiiustation_jit_rw()/rx(), which nest.  Without a codegen area the
 * emulator runs Lightrec's interpreter-free fallback: the dynarec option is
 * forced off and PCSX-ReARMed's interpreter is used.
 */
#include <coreinit/codegen.h>
#include <coreinit/core.h>
#include <coreinit/cache.h>
#include <stddef.h>
#include "platform.h"

uint32_t wiiustation_jit_size = 0;
static void *jit_base;
static int probed, depth;

static void probe(void)
{
	void *addr = NULL;
	uint32_t size = 0;
	if (probed) return;
	probed = 1;
	OSGetCodegenVirtAddrRange(&addr, &size);
	if (!addr || size < 0x100000) return;
	/* Only the codegen core may switch the area; the emulator runs on the
	 * main thread, which is that core when codegen is granted. */
	if (OSGetCodegenCore() != OSGetCoreId()) return;
	if (!OSSwitchSecCodeGenMode(CODEGEN_RW_)) return;
	OSSwitchSecCodeGenMode(CODEGEN_R_X);
	jit_base = addr;
	wiiustation_jit_size = size;
}

int jit_available(void)
{
	probe();
	return jit_base != NULL;
}

void *wiiustation_jit_base(void)
{
	probe();
	return jit_base;
}

void wiiustation_jit_rw(void)
{
	if (!jit_base) return;
	if (depth++ == 0) OSSwitchSecCodeGenMode(CODEGEN_RW_);
}

void wiiustation_jit_rx(void)
{
	if (!jit_base || depth <= 0) return;
	if (--depth == 0) OSSwitchSecCodeGenMode(CODEGEN_R_X);
}

/* Lightrec's code_inv callback (libpcsxcore/lightrec/mem_wiiu.c). */
void wiiu_clear_cache(void *start, void *end)
{
	uint32_t len = (uint32_t)((char *)end - (char *)start);
	DCFlushRange(start, len);
	ICInvalidateRange(start, len);
}

/* newlib on Cafe OS has no sysconf(); GNU lightning asks for the page size. */
#include <unistd.h>
long sysconf(int name)
{
	switch (name) {
	case _SC_PAGESIZE:          return 4096;
	case _SC_NPROCESSORS_CONF:
	case _SC_NPROCESSORS_ONLN:  return 3;
	default:                    return -1;
	}
}
