# Host test for Core/MIDI/MidiNoteMap.c (S083). Host cc only.
CC     ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
SRC     = ../../Core/MIDI/MidiNoteMap.c

notemap_test: notemap_test.c $(SRC) ../../Core/MIDI/MidiNoteMap.h
	$(CC) $(CFLAGS) -DMIDI_NOTEMAP_HOST_TEST -I../../Core/MIDI \
	    -o $@ notemap_test.c $(SRC)

run: notemap_test
	./notemap_test

clean:
	rm -f notemap_test

.PHONY: run clean
