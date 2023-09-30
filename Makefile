zeros-linux: zeros.c
	gcc zeros.c -o zeros-linux -lportaudio -lm -pthread -std=c99 -Wall

zeros-mac: zeros.c
	gcc \
    -I/opt/homebrew/include/ \
    -L/opt/homebrew/lib/ \
    -F/System/Library/PrivateFrameworks \
    -framework CoreMIDI \
    -framework CoreFoundation \
    -framework CoreAudio \
    -framework Foundation \
    -lportaudio \
    zeros.c -o zeros-mac -std=c99 -Wall

run-linux: zeros-linux
	./zeros-linux \
    $(CURDIR)/device-index $(CURDIR)/current-voice $(CURDIR)/current-volume $(CURDIR)/current-gate


run-mac: zeros-mac
	./zeros-mac \
    $(CURDIR)/device-index $(CURDIR)/current-voice $(CURDIR)/current-volume $(CURDIR)/current-gate
