# Two implementations live here in parallel:
#
#   zeros.c    the original single-file version    -> zeros-linux, zeros-mac
#   zeros2.c   the rewrite, split into modules     -> zeros2-linux, zeros2-mac
#
# See README, "How it works", for what the rewrite does differently.

MAC_FLAGS = \
    -I/opt/homebrew/include/ \
    -L/opt/homebrew/lib/ \
    -F/System/Library/PrivateFrameworks \
    -framework CoreMIDI \
    -framework CoreFoundation \
    -framework CoreAudio \
    -framework Foundation \
    -lportaudio

# ------------------------------------------------------------- original ---

zeros-linux: zeros.c
	gcc zeros.c -o zeros-linux -lportaudio -lm -pthread -std=c99 -Wall

zeros-mac: zeros.c
	gcc $(MAC_FLAGS) zeros.c -o zeros-mac -std=c99 -Wall

pa: paex_read_write_wire.c
	gcc $(MAC_FLAGS) paex_read_write_wire.c -o paex_read_write_wire -std=c99 -Wall

run-linux: zeros-linux
	./zeros-linux \
    $(CURDIR)/device-index $(CURDIR)/current-voice $(CURDIR)/current-volume $(CURDIR)/current-gate

run-mac: zeros-mac
	./zeros-mac \
    $(CURDIR)/device-index $(CURDIR)/current-voice $(CURDIR)/current-volume $(CURDIR)/current-gate

# -------------------------------------------------------------- rewrite ---

DSP = pitch.c synth.c engine.c
AUDIO_SRC = zeros2.c selftest.c $(DSP)
WARN = -std=c99 -Wall -Wextra

zeros2-linux: $(AUDIO_SRC)
	gcc $(AUDIO_SRC) -o zeros2-linux -lportaudio -lm -pthread $(WARN) -O2

zeros2-mac: $(AUDIO_SRC)
	gcc $(MAC_FLAGS) $(AUDIO_SRC) -o zeros2-mac $(WARN) -O2

# Runs the same engine over a file: see README, "Developing voices offline".
zeros2-offline: offline.c $(DSP)
	gcc offline.c $(DSP) -o zeros2-offline -lm $(WARN) -O2

run2-linux: zeros2-linux
	./zeros2-linux \
    $(CURDIR)/device-index $(CURDIR)/current-voice $(CURDIR)/current-volume $(CURDIR)/current-gate $(CURDIR)/current-fifth $(CURDIR)/current-sustain

run2-mac: zeros2-mac
	./zeros2-mac \
    $(CURDIR)/device-index $(CURDIR)/current-voice $(CURDIR)/current-volume $(CURDIR)/current-gate $(CURDIR)/current-fifth $(CURDIR)/current-sustain

# ----------------------------------------------------------------------- --

clean:
	rm -f zeros-linux zeros-mac zeros2-linux zeros2-mac zeros2-offline

.PHONY: clean run-linux run-mac run2-linux run2-mac
