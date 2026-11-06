#!/bin/bash
# Host filesystem harness: the real filesystem.c, asyncfatfs, storage and data
# modules on a RAM-backed FAT32 card image (ramsd.c). Everything outside
# storage (DSP voices, menu, sequencer, LEDs) is stubbed with zero-returning
# functions generated from the link errors.
#
#   tools/fs_test/build.sh                      -> build/fs_test/harness
#   tools/fs_test/mkimg.sh <card dir> card.img  -> MBR + FAT32 image
#   build/fs_test/harness card.img 108 k:100 s:002 p:000
#       bare number = Bank Load (all 16 Scenes, Replace), k:/s:/p: = root
#       Kit/Scene/Pattern Load into Scene 0. Prints status, error code, SD
#       block reads/writes and simulated time (0.6 ms read, 1.5 ms write).
# Needs gcc-multilib (-m32) and, for mkimg.sh, dosfstools + mtools.
set -e
cd "$(dirname "$0")/../.."
OUT=build/fs_test; mkdir -p $OUT/obj
INC=$(grep -oE '\-I[^ ]+' Makefile | sort -u | tr '\n' ' ')
for f in $(cat tools/fs_test/sources.txt); do
  b=$(echo $f | tr '/' '_')
  gcc -m32 -fshort-enums -E -w $INC -include stddef.h $f \
    | perl -0pe 's/__asm\s+(volatile\s*)?\((?:[^;])*?\)\s*;/;/gs; s/__asm__\s+(__volatile__\s*)?\((?:[^;])*?\)\s*;/;/gs' > $OUT/obj/$b.i
  gcc -m32 -fshort-enums -O0 -g -w -c -o $OUT/obj/$b.o $OUT/obj/$b.i
done
gcc -m32 -fshort-enums -w -g $INC -c -o $OUT/obj/zz_ramsd.o tools/fs_test/ramsd.c
gcc -m32 -fshort-enums -w -g $INC -c -o $OUT/obj/zz_harness.o tools/fs_test/harness.c
# Stub every remaining undefined symbol (variables get zeroed storage).
gcc -m32 -o $OUT/harness $OUT/obj/*.o -lm 2>&1 | grep "undefined reference to" | grep -v "more undefined" \
  | sed "s/.*undefined reference to .//; s/'$//" | sort -u > $OUT/undef.txt || true
python3 -I - $OUT <<'PY'
import re, sys
out = sys.argv[1]
var = {'menu_activePage': 'unsigned char menu_activePage;',
       'mixer_audioRouting': 'unsigned char mixer_audioRouting[6];',
       'mixer_decimation_rate': 'float mixer_decimation_rate[6];',
       'parameter_values': 'unsigned char parameter_values[4096];',
       'parameters2': 'unsigned char parameters2[4096];',
       'seq_activePattern': 'unsigned char seq_activePattern;',
       'seq_perTrackActive': 'unsigned char seq_perTrackActive;',
       'seq_perTrackPattern': 'unsigned char seq_perTrackPattern[7];',
       'velocityModulators': 'char velocityModulators[4096];',
       '_efxbuf': 'char _efxbuf[1];', '_sfxbuf': 'char _sfxbuf[1];',
       'seq_getBpm': 'unsigned short seq_getBpm(void){return 120;}',
       'GetRngValue': 'unsigned int GetRngValue(void){return 12345;}'}
lines = []
for s in sorted(set(open(out + '/undef.txt').read().split())):
    if s == 'main' or not re.fullmatch(r'[A-Za-z_]\w*', s):
        continue
    lines.append(var.get(s, f'int {s}(){{return 0;}}'))
open(out + '/stubs.c', 'w').write('\n'.join(lines) + '\n')
PY
gcc -m32 -w -c -o $OUT/obj/zz_stubs.o $OUT/stubs.c
gcc -m32 -g -o $OUT/harness $OUT/obj/*.o -lm
echo "built $OUT/harness"
