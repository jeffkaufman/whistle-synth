# Usage

## Build

1. Check out as ~/whistle-synth

2. Install dependencies:
   ```
   sudo apt install portaudio19-dev python3-evdev python3-mido python3-rtmidi
   ```

3. Build it:
   ```
    make zeros-linux
   ```

It will detect pitches and generate audio.

To run on boot, `/etc/systemd/system/whistle-synth.service` should have:

```
[Unit]
Description=Pitch Detection and	Synthesis

[Service]
ExecStart=/home/jeffkaufman/whistle-synth/zeros-linux /home/jeffkaufman/whistle-synth/device-index /home/jeffkaufman/whistle-synth/current-voice /home/jeffkaufman/whistle-synth/current-volume /home/jeffkaufman/whistle-synth/current-gate
Restart=always
KillSignal=SIGQUIT
Type=simple

[Install]
WantedBy=multi-user.target
```

To support changing voices while headless,
`/etc/systemd/system/whistle-synth-kbd.service` should have:

```
[Unit]
Description=Keyboard Control for Pitch Synthesis

[Service]
ExecStart=/usr/bin/python3 /home/jeffkaufman/whistle-synth/kbd.py
Restart=always
KillSignal=SIGQUIT
Type=simple

[Install]
WantedBy=multi-user.target
```

Then:

```
sudo systemctl enable whistle-synth-kbd
sudo systemctl enable whistle-synth
sudo systemctl daemon-reload
```

Set levels for consistency:

```
$ alsamixer
> F6 select "USB Audio Device"
> F5 [All]
> Speaker: 100
> Mic: 100
> Capture: 100
```

## Run

1. Put on headphones, use a directional mic, or otherwise avoid letting the
   output of this program mix with the input.

2. Run it and whistle:
   ```
     make run-linux
   ```
Or
   ```
     make run-mac
   ```

It will generate audio.

Keys 0-8 on the keypad should select voices.  Voices 0 through 6
expect whistling; 7 and 8 singing.

## Microphone tips:

* Works best with a directional microphone with a windscreen (vocal mics like
  the E835 or SM58 have one built in).

* I use a Sennheiser E835 with an xlr to 3.5mm adapter into a USB
  sound card.  This isn't how the microphone is designed to be used
  (it wants a pre-amp) but it works well enough and it's nice not to
  have another piece of hardware.

* You want to be as close to the microphone as you can bear.

## Raspberry PI Setup

1. Install Raspberry Pi Os Lite (we don't want the desktop environment)
1. `sudo apt-get update && sudo apt-get upgrade`
1. `sudo raspi-config`
    1. "Interface Options"
        1. "Enable SSH"
    1. "Localisation Options"
        1. "WLAN Country"
    1. "System Options"
        1. "Wireless LAN"
1. Add regular public key to `~/.ssh/authorized_keys`
1. Change default password (`passwd`) 
1. `sudo apt install git emacs`
1. https://www.jefftk.com/p/you-should-be-logging-shell-history
1. `alsamixer`
    1. select sound card "USB Audio Device"
    1. Set Speaker, Mic, and Capture to 100% volume

### Future

Look into low-latency options.  See http://tedfelix.com/linux/linux-midi.html
and https://wiki.linuxaudio.org/wiki/raspberrypi

