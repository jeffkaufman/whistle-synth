import evdev # sudo apt install python3-evdev
import glob
import time
import os
import sys

JAMMER_CONFIG_LENGTH = 10

whistle_voice_fname = None
jammer_config_fname = None

jammer_config = [' ']*JAMMER_CONFIG_LENGTH

def find_keyboard():
    keyboards = glob.glob("/dev/input/by-id/*kbd")
    while not keyboards:
        sleep(1)
        keyboards = glob.glob("/dev/input/by-id/*kbd")
    return keyboards[0]

def run(device_id):
    device = evdev.InputDevice(device_id)
    for event in device.read_loop():
        if event.type == evdev.ecodes.EV_KEY:
            event = evdev.categorize(event)
            if event.keystate == 1: # keydown
                handle_key(event.keycode)

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

def write_jammer_config():
    with open(jammer_config_fname, 'w') as outf:
        outf.write(''.join(jammer_config))

def handle_key(keycode):
    if keycode in whistle_voice_keys:
        with open(whistle_voice_fname, 'w') as outf:
            outf.write(str(whistle_voice_keys[keycode]))
    elif jammer_config_fname and keycode in jammer_config_keys:
        config_index = jammer_config_keys[keycode]
        if config_index == 9:
            for i in range(JAMMER_CONFIG_LENGTH):
                jammer_config[i] = ' '
        else:
            jammer_config[config_index] = (
                'x' if jammer_config[config_index] == ' ' else ' ')
        write_jammer_config()
    else:
        print(keycode)

def start():
    global whistle_voice_fname
    global jammer_config_fname

    if len(sys.argv) <= 1 or len(sys.argv) > 3:
        print("usage: kbd.py <whistle_voice_fname> [jammer_config_fname]");
        return
    if len(sys.argv) > 1:
        whistle_voice_fname = sys.argv[1]
    if len(sys.argv) > 2:
        jammer_config_fname = sys.argv[2]

    if jammer_config_fname:
        write_jammer_config()

    device_id = find_keyboard()
    run(device_id)

if __name__ == "__main__":
    start()
