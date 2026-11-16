# CPU bench (`tools/cpu_bench/`)

Host build of the real render path (`mixer_calcNextSampleBlock`: voices,
filters, Effect, bus compressor) on a RAM FAT32 card, built like
`tools/fs_test` with `-O2` and host saturation intrinsics
(`host_intrinsics.h`). Count instructions with callgrind: they are
deterministic, unlike wall-clock time on a shared host.

    tools/cpu_bench/build.sh
    tools/fs_test/mkimg.sh <card dir> card.img 600
    valgrind --tool=callgrind --toggle-collect=mixer_calcNextSampleBlock \
      build/cpu_bench/harness card.img <bank> <blocks> <scene> <fx 0|1> pat
    callgrind_annotate --inclusive=yes callgrind.out.*

Modes: `... <scene> <fx> pat` plays the Scene's pattern at 130 BPM;
`... <scene> <fx> <voice mask> [every]` retriggers the masked voices every
`every` blocks; `... peaks` renders after rumble edits, pattern changes and
RANDOM draws in separate wrappers. `BENCH_DUMP=file` writes the stereo
output, `BENCH_NOSLEEP=1` disables the silent-voice sleep (for A/B checks).

Budget: 216 MHz / 44,108 Hz x 32 frames = 156,700 cycles per block. Host
instructions are not device cycles; the device `cpu` reading of 94 % on a
comparable kit + DlyVerb suggests about 1.3 cycles per host instruction.
