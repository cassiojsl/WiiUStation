/* GTE big-endian check: build for the host and for powerpc-linux, run both
 * (the PPC one under qemu-ppc) and compare the outputs.  See run script. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "psxcommon.h"
#include "r3000a.h"
#include "gte.h"
/* stubs */
psxRegisters psxRegs;
static uint32_t seed = 12345;
static uint32_t rnd(void){ seed = seed*1103515245u+12345u; return seed ^ (seed>>16); }
int main(void){
  static const u32 ops[] = { 0x01, 0x06, 0x0c, 0x10, 0x11, 0x12, 0x13, 0x14, 0x16, 0x1b, 0x1c, 0x1e, 0x20, 0x28, 0x29, 0x2a, 0x2d, 0x2e, 0x30, 0x3d, 0x3e, 0x3f };
  psxCP2Regs *r = (psxCP2Regs *)&psxRegs.CP2D;
  uint32_t hash = 0;
  for (int t = 0; t < 300; t++) {
    for (int i = 0; i < 32; i++) {
      uint32_t v = rnd();
      if (i < 6 && (rnd() & 1)) v &= 0x00ff00ff; /* smaller vertices */
      MTC2(r, v, i);
    }
    for (int i = 0; i < 32; i++) CTC2(r, rnd() & ((i==26||i==27||i==28)?0xffff:0xffffffff), i);
    u32 op = ops[t % (sizeof(ops)/4)];
    u32 code = 0x4a000000 | op | ((rnd()&1)<<19) | ((rnd()&1)<<10) | ((rnd()&3)<<17) | ((rnd()&3)<<15) | ((rnd()&3)<<13);
    gteDispatch(r, code);
    printf("T%d op%02x", t, op);
    for (int i = 0; i < 32; i++) printf(" d%d=%08x", i, MFC2(r, i));
    for (int i = 0; i < 32; i++) printf(" c%d=%08x", i, r->CP2C.r[i]);
    printf("\n");
  }
  printf("HASH %08x\n", hash);
  return 0;
}
