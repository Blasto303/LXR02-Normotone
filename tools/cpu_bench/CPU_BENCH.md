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
`BENCH_PLAITS=1` plays the Scene in PLAITS mode (its kit converted to the
Plaits engines; `2` builds the conversion tables first, as after boot).
`... plaits` runs the PLAITS scenario: toggle, pattern changes, KIT swap,
`fixe`, toggle off, own kits intact, and sample p-lock redirections.

Budget: 216 MHz / 44,108 Hz x 32 frames = 156,700 cycles per block. Host
instructions are not device cycles; the device `cpu` reading of 94 % on a
comparable kit + DlyVerb suggests about 1.3 cycles per host instruction.

## Foreground and interrupts (`fg_bench.c`, `build_fg.sh`)

Everything except the DSP render: TIM3 sequencer and TIM7 LCD at their real
rates, one main-loop pass per ms, the render-side control work per block,
with an optional gesture script (effect page, pot turns, pattern changes,
RANDOM, KIT page). Peripheral ranges are mapped as memory so the real LCD,
LED and trigger-jack drivers run. Read the `isr_*`, `svc_*`, `ctl_*`, `ev_*`
wrappers in callgrind (`--threshold=100 --auto=no`).

    tools/cpu_bench/build_fg.sh
    valgrind --tool=callgrind build/cpu_bench_fg/harness card.img 108 15 1
