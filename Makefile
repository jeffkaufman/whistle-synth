zeros-linux: zeros.c
	gcc -lportaudio -lm \
	  zeros.c -o zeros-linux -std=c99 -Wall

zeros-m: zeros.m
	gcc \
    -F/System/Library/PrivateFrameworks \
	  -framework CoreMIDI \
    -framework CoreFoundation \
    -framework CoreAudio \
    -framework Foundation \
	  -lportaudio \
	  zeros.m -o zeros-mac -std=c99 -Wall

zeros-osc: zeros.m
	gcc \
    -F/System/Library/PrivateFrameworks \
	  -framework CoreMIDI \
    -framework CoreFoundation \
    -framework CoreAudio \
    -framework Foundation \
	  -lportaudio \
	  -llo \
	  -DUSE_OSC \
	  zeros.m -o zeros-osc -std=c99 -Wall

run-linux: zeros-linux
	./zeros-linux

run-osc: zeros-osc
	./zeros-osc

run-mac: zeros-mac
	./zeros-mac
