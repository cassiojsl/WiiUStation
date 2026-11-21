#!/bin/sh
# Compares PCSX-ReARMed's GTE on little-endian (host) and big-endian
# (powerpc-linux under qemu-ppc). Needs: gcc, powerpc-linux-gnu-gcc, qemu-ppc.
set -e
D=$(cd "$(dirname "$0")" && pwd)
R=$D/../vendor/pcsx_rearmed
T=$(mktemp -d)
for cc in gcc powerpc-linux-gnu-gcc; do
  $cc -O1 -static -I$R/libpcsxcore -I$R/include -I$R -o $T/$cc "$D/gte_endian_test.c" \
      $R/libpcsxcore/gte.c $R/libpcsxcore/gte_divider.c
done
$T/gcc > $T/le.txt
qemu-ppc $T/powerpc-linux-gnu-gcc > $T/be.txt
if cmp -s $T/le.txt $T/be.txt; then echo "GTE: big-endian matches little-endian"; else echo "GTE: MISMATCH"; exit 1; fi
