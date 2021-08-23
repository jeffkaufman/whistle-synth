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
        time.sleep(1)
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
    'KEY_1': 0,
    'KEY_2': 1,
    'KEY_3': 2,
    'KEY_4': 4,
    'KEY_5': 6,
}

def handle_key(keycode, midiport):
    if keycode in whistle_voice_keys:
        with open(whistle_voice_fname, 'w') as outf:
            outf.write(str(whistle_voice_keys[keycode]))
    elif keycode >= 'KEY_A' and keycode <= 'KEY_Z':
        pseudo_note = ord(keycode[-1])
        print(pseudo_note)
        midiport.send(
            mido.Message('note_on',
                         note=pseudo_note))
    else:
        print(keycode)

def start():
    global whistle_voice_fname

    if len(sys.argv) != 2:
        print("usage: kbd.py <whistle_voice_fname>");
        return
    whistle_voice_fname = sys.argv[1]

    device_id = find_keyboard()
    with mido.open_output('mido-keypad', virtual=True) as midiport:
        run(device_id, midiport)

if __name__ == "__main__":
    start()
