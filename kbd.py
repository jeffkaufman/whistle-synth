import evdev # sudo apt install python3-evdev
import glob
import time
import os
import sys
import mido # sudo apt install python3-mido python3-rtmidi
import subprocess

JAMMER_CONFIG_LENGTH = 10

config_dir = os.path.dirname(__file__)
whistle_voice_fname = os.path.join(config_dir, "current-voice")
whistle_volume_fname = os.path.join(config_dir, "current-volume")

digits_read = []
digit_note_to_send = None

def find_keyboard():
    keyboards = glob.glob("/dev/input/by-id/*kbd")
    while not keyboards:
        time.sleep(1)
        keyboards = glob.glob("/dev/input/by-id/*kbd")
    return keyboards[0]

modifiers = {
    'KEY_RIGHTALT': False,
    'KEY_RIGHTSHIFT': False,
    'KEY_RIGHTCTRL': False,
    'KEY_LEFTALT': False,
    'KEY_LEFTSHIFT': False,
    'KEY_LEFTCTRL': False,
}

def run(device_id, midiport):
    device = evdev.InputDevice(device_id)
    for event in device.read_loop():
        if event.type == evdev.ecodes.EV_KEY:
            event = evdev.categorize(event)

            if event.keycode in modifiers and event.keystate in [0, 1]:
                modifiers[event.keycode] = (event.keystate == 1)
            elif event.keystate == 1: # keydown
                handle_key(event.keycode, midiport)

def read_number(fname):
    with open(fname) as inf:
        return int(inf.read().strip())

def write_number(val, fname):
    if val < 0:
        val = 0
    if val > 9:
        val = 9
    with open(fname, 'w') as outf:
        outf.write(str(val))

def current_voice():
    return read_number(whistle_voice_fname)

def volume_fname():
    return os.path.join(config_dir, "configured-volume-%s" % current_voice())

def current_volume():
    fname = volume_fname()
    if os.path.exists(fname):
        return read_number(fname)
    return 5

def save_volume(new_value):
    write_number(new_value, volume_fname())
    write_number(new_value, whistle_volume_fname)

def volume_change(increment):
    save_volume(current_volume() + increment)

whistle_voice_keys = {
    'KEY_KP1': 1,
    'KEY_KP2': 2,
    'KEY_KP3': 3,
    'KEY_KP4': 4,
    'KEY_KP5': 5,
    'KEY_KP6': 6,
    'KEY_KP7': 7,
    'KEY_KP8': 8,
    'KEY_KP9': 9,
    'KEY_KP0': 0,
}

# Remaining available whistle options:
#
#   KEY_NUMLOCK
#   KEY_KPSLASH
#   KEY_KPASTERISK
#   KEY_BACKSPACE
#   KEY_KPENTER
#   KEY_KPDOT

keycodes = {
    'KEY_LEFTBRACE': 'KEY_[',
    'KEY_RIGHTBRACE': 'KEY_]',
    'KEY_SEMICOLON': 'KEY_;',
    'KEY_APOSTROPHE': 'KEY_\'',
    'KEY_COMMA': 'KEY_,',
    'KEY_DOT': 'KEY_.',
    'KEY_SLASH': 'KEY_/',
    'KEY_BACKSLASH': 'KEY_\\',
    'KEY_GRAVE': 'KEY_`',
    'KEY_EQUAL': 'KEY_=',
    'KEY_MINUS': 'KEY_-',
}

def handle_key(keycode, midiport):
    global state
    global digit_note_to_send

    keycode = keycodes.get(keycode, keycode)

    if (len(keycode) == len('KEY_0') and
        'KEY_0' <= keycode <= 'KEY_9' and
        digit_note_to_send):

        digits_read.append(keycode[-1])
        if len(digits_read) == 3:
            val = int("".join(digits_read))
            if 0 <= val <= 127:
                midiport.send(
                    mido.Message('note_on',
                                 note=digit_note_to_send,
                                 velocity=val))
                print("sending %s %s" % (digit_note_to_send, val))
            digits_read.clear()
            digit_note_to_send = None
        return

    digit_note_to_send = None
    digits_read.clear()

    if keycode in whistle_voice_keys:
        write_number(whistle_voice_keys[keycode], whistle_voice_fname)
        save_volume(current_volume())
    elif keycode == 'KEY_KPMINUS':
        volume_change(-1)
    elif keycode == 'KEY_KPPLUS':
        volume_change(+1)
    elif keycode.startswith("KEY_F") and keycode[len("KEY_F"):].isdigit():
        fn_digit = int(keycode[len("KEY_F"):])
        pseudo_note = ord('a') + fn_digit
        print(pseudo_note)

        midiport.send(
            mido.Message('note_on',
                         note=pseudo_note))
    elif len(keycode) == len('KEY_A'):
        pseudo_note = ord(keycode[-1])
        print(pseudo_note)

        if keycode == 'KEY_P':
            digit_note_to_send = pseudo_note
        else:
            midiport.send(
                mido.Message('note_on',
                             note=pseudo_note))
    else:
        print(keycode)

def start_services(*services):
    for service in services:
        subprocess.run(["service", service, "start"])

def start():
    if len(sys.argv) != 1:
        print("usage: kbd.py");
        return

    device_id = find_keyboard()

    if "SEM_HCT" in device_id:
        # Keypad found: run whistle
        start_services("pitch-detect")
    else:
        # No keypad: run jammer
        start_services("fluidsynth", "jammer")

    with mido.open_output('mido-keypad', virtual=True) as midiport:
        run(device_id, midiport)

if __name__ == "__main__":
    start()
