import evdev # sudo apt install python3-evdev
import glob
import time
import os
import sys
import mido # sudo apt install python3-mido python3-rtmidi

JAMMER_CONFIG_LENGTH = 10

whistle_voice_fname = None

def find_keyboard():
    keyboards = glob.glob("/dev/input/by-id/*kbd")
    while not keyboards:
        sleep(1)
        keyboards = glob.glob("/dev/input/by-id/*kbd")
    return keyboards[0]

def run(device_id, midiport):
    device = evdev.InputDevice(device_id)
    for event in device.read_loop():
        if event.type == evdev.ecodes.EV_KEY:
            event = evdev.categorize(event)
            if event.keystate == 1: # keydown
                handle_key(event.keycode, midiport)

whistle_voice_keys = {
    'KEY_NUMLOCK': 0,
    'KEY_KPSLASH': 1,
    'KEY_KPASTERISK': 2,
    'KEY_KPMINUS': 3,
    'KEY_KPPLUS': 4,
    'KEY_BACKSPACE': 5,
    'KEY_KPDOT': 6
}

jammer_config_keys = {}
for i in range(10):
    jammer_config_keys['KEY_%s' % i] = i
    jammer_config_keys['KEY_KP%s' % i] = i

def handle_key(keycode, midiport):
    if keycode in whistle_voice_keys:
        with open(whistle_voice_fname, 'w') as outf:
            outf.write(str(whistle_voice_keys[keycode]))
    elif keycode in jammer_config_keys:
        midiport.send(
            mido.Message('note_on',
                         note=jammer_config_keys[keycode]))
    else:
        print(keycode)

def start():
    global whistle_voice_fname

    if len(sys.argv) != 2:
        print("usage: kbd.py <whistle_voice_fname>");
        return
    whistle_voice_fname = sys.argv[1]

    device_id = find_keyboard()
    with mido.open_output('mido-kbd', virtual=True) as midiport:
        run(device_id, midiport)

if __name__ == "__main__":
    start()
