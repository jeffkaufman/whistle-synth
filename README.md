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

On a Mac, instead install `brew install portaudio` and `make zeros-mac`.  It
prefers a sound card whose name starts with `USB_SOUND_CARD_PREFIX` in
`zeros.c` and that does both input and output (a Scarlett, say); failing that
it falls back to the default input and output devices, which on a Mac are
separate.

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

## Latency

Tuned for a Mac with a Scarlett 2i2.  On startup it prints the latency it
actually got, and prints a running `xruns:` count if it can't keep up.

Three things matter, in order:

1. **PortAudio's callback API**, not the blocking API.  `Pa_ReadStream` /
   `Pa_WriteStream` layer their own ring buffers on top of the callback
   machinery and never got below about 27ms no matter how they were tuned.
2. **`suggestedLatency`**, with `paFramesPerBufferUnspecified` so the host
   API hands over its native buffer size.  This used to be set to the
   device's `defaultLowInputLatency`, which sounds low but isn't --
   PortAudio sizes its buffers from it.
3. **`paMacCorePro`** (mac only).  Without it CoreAudio keeps its own
   sample rate and buffer size and quietly converts; with it the device
   gets reconfigured to match us.  Note this changes the device's buffer
   size for other apps while we're running.

Measured acoustic round trip on a Scarlett 2i2, output to headphones and
back in through the mic:

| config | round trip |
|---|---|
| blocking API, device "default low" latency | 80.1ms |
| blocking API, minimum buffers | 27.6ms |
| callback API, 128-frame buffers | 16.0ms |
| callback API, frames unspecified, 44100 | 6.5ms |
| callback API + `paMacCorePro`, 48000 | **5.0ms** |

The hardware floor is 176 frames (3.7ms at 48k) of fixed converter and
safety-offset latency, from `kAudioDevicePropertyLatency` and
`kAudioDevicePropertySafetyOffset`, so there is not much left to win.
Dropping PortAudio for raw AudioUnits would be chasing the ~1ms between
5.0ms and that floor.  Asking for buffers below ~20 frames measures the
same round trip and can stall the callback outright.

`SAMPLE_RATE` is 48000 because the pipeline is a fixed number of *frames*
deep, so a faster rate is fewer milliseconds.  96k and 192k were measured
and bought only another 0.4ms for 2-4x the CPU and delay buffer.  The
pitch-detection bounds derive from `SAMPLE_RATE`, so changing it keeps
them fixed in Hz.

### Future

The mac-specific latency work is under `#ifdef __APPLE__` so the linux
build should still compile, but `zeros-linux` has not been built or run
since any of it landed -- treat it as untested.  If you go back to the
Pi, `SUGGESTED_LATENCY` is the knob to raise until the xruns stop.  See http://tedfelix.com/linux/linux-midi.html and
https://wiki.linuxaudio.org/wiki/raspberrypi

