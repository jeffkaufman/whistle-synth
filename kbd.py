import evdev # sudo apt install python3-evdev
import glob
import time
import os
current_voice_fname = os.path.join(os.path.dirname(__file__), "current-voice")

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

keys = {}
for i in range(10):
    keys['KEY_%s' % i] = i
    keys['KEY_KP%s' % i] = i
    
def handle_key(keycode):
    if keycode in keys:
        with open(current_voice_fname, 'w') as outf:
            outf.write(str(keys[keycode]))
    print(keycode)
                
def start():
    device_id = find_keyboard()
    run(device_id)

if __name__ == "__main__":
    start()
