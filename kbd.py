import evdev # sudo apt install python3-evdev
import glob
import time
import os
whistle_voice_fname = os.path.join(os.path.dirname(__file__), "current-voice")

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
    
def handle_key(keycode):
    if keycode in whistle_voice_keys:
        with open(whistle_voice_fname, 'w') as outf:
            outf.write(str(whistle_voice_keys[keycode]))
    else:
        print(keycode)
                
def start():
    device_id = find_keyboard()
    run(device_id)

if __name__ == "__main__":
    start()
