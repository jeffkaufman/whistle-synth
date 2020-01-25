zeros: zeros.m
	gcc \
    -F/System/Library/PrivateFrameworks \
	  -framework CoreMIDI \
    -framework CoreFoundation \
    -framework CoreAudio \
    -framework Foundation \
	  -lportaudio \
	  zeros.m -o zeros -std=c99 -Wall

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

run: zeros
	./zeros

run-osc: zeros-osc
	./zeros-osc

