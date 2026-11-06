#!/bin/bash
# mkimg.sh <card dir> <image>  : MBR + FAT32 partition at 1 MiB, card dir copied in
set -e
D=$1; IMG=$2; SIZE_MB=${3:-160}
rm -f $IMG; truncate -s ${SIZE_MB}M $IMG
python3 -I - "$IMG" "$SIZE_MB" <<'PY'
import sys,struct
img,mb=sys.argv[1],int(sys.argv[2]); total=mb*2048; start=2048; n=total-start
mbr=bytearray(512); e=bytearray(16); e[0]=0; e[4]=0x0C; struct.pack_into('<II',e,8,start,n)
mbr[446:462]=e; mbr[510]=0x55; mbr[511]=0xAA
open(img,'r+b').write(mbr)
PY
mkfs.fat -F 32 -s 8 --offset 2048 -n LXR $IMG $(( (SIZE_MB*2048-2048)/2 )) >/dev/null
export MTOOLS_SKIP_CHECK=1
( cd $D && for f in * .[!.]*; do [ -e "$f" ] || continue; mcopy -s -i $IMG@@1M "$f" ::/ ; done )
