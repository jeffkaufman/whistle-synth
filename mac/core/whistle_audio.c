// CoreAudio device enumeration and the audio streams themselves.
//
// This replaces what PortAudio did for the command-line build.  It is written
// against the public AudioUnit/HAL API only, which matters for two reasons:
// the App Store will not take a private-framework link, and the sandbox will
// not load a dylib from outside the bundle.
//
// Two shapes of stream, because the fast one is not always available:
//
//   duplex  input and output are the same device, so one AUHAL unit does
//           both and the callback has the input in hand when it renders.
//           This is an interface like a Scarlett, and it is the low-latency
//           path.
//
//   split   input and output are different devices -- a laptop's built-in
//           microphone and a pair of headphones -- which means two clocks
//           that drift, so the input goes through a ring buffer.  It costs a
//           little latency and the occasional resync.
//
// The split path is only tolerable because of how the engine is built: the
// synth free-runs and never reads the input, so input and output do not have
// to stay sample-aligned.  A resynced sample perturbs the pitch detector for
// one analysis window and never reaches the output directly.

#include "whistle.h"
#include "whistle_internal.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char last_error[256];

static void fail(const char* what, OSStatus status) {
  if (status == noErr) {
    snprintf(last_error, sizeof(last_error), "%s", what);
  } else {
    snprintf(last_error, sizeof(last_error), "%s (CoreAudio error %d)", what,
             (int)status);
  }
}

const char* whistle_last_error(void) {
  return last_error;
}

/* ----------------------------------------------------------- properties --- */

static OSStatus get_property(AudioObjectID object,
                             AudioObjectPropertySelector selector,
                             AudioObjectPropertyScope scope,
                             void* out,
                             UInt32 size) {
  AudioObjectPropertyAddress address = {
    selector, scope, kAudioObjectPropertyElementMain };
  UInt32 io_size = size;
  return AudioObjectGetPropertyData(object, &address, 0, NULL, &io_size, out);
}

static bool copy_cfstring(AudioObjectID object,
                          AudioObjectPropertySelector selector,
                          AudioObjectPropertyScope scope,
                          char* out,
                          size_t out_size) {
  out[0] = '\0';
  CFStringRef string = NULL;
  if (get_property(object, selector, scope, &string, sizeof(string)) != noErr ||
      !string) {
    return false;
  }
  bool ok = CFStringGetCString(string, out, (CFIndex)out_size,
                               kCFStringEncodingUTF8);
  CFRelease(string);
  return ok;
}

// Channels the device offers in one direction.  A device with none in a
// direction is not usable for it, which is how we tell inputs from outputs.
static int channel_count(AudioDeviceID device, AudioObjectPropertyScope scope) {
  AudioObjectPropertyAddress address = {
    kAudioDevicePropertyStreamConfiguration, scope,
    kAudioObjectPropertyElementMain };
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(device, &address, 0, NULL, &size)
          != noErr || size == 0) {
    return 0;
  }
  AudioBufferList* list = (AudioBufferList*)malloc(size);
  if (!list) {
    return 0;
  }
  int channels = 0;
  if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, list)
          == noErr) {
    for (UInt32 i = 0; i < list->mNumberBuffers; i++) {
      channels += (int)list->mBuffers[i].mNumberChannels;
    }
  }
  free(list);
  return channels;
}

static AudioDeviceID default_device(bool input) {
  AudioDeviceID device = kAudioObjectUnknown;
  get_property(kAudioObjectSystemObject,
               input ? kAudioHardwarePropertyDefaultInputDevice
                     : kAudioHardwarePropertyDefaultOutputDevice,
               kAudioObjectPropertyScopeGlobal, &device, sizeof(device));
  return device;
}

/* -------------------------------------------------------------- devices --- */

int whistle_list_devices(struct WhistleDevice* out, int max) {
  AudioObjectPropertyAddress address = {
    kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain };
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0,
                                     NULL, &size) != noErr) {
    return 0;
  }
  int count = (int)(size / sizeof(AudioDeviceID));
  if (count <= 0) {
    return 0;
  }
  AudioDeviceID* ids = (AudioDeviceID*)malloc(size);
  if (!ids) {
    return 0;
  }
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL,
                                 &size, ids) != noErr) {
    free(ids);
    return 0;
  }

  AudioDeviceID default_in = default_device(true);
  AudioDeviceID default_out = default_device(false);

  int found = 0;
  for (int i = 0; i < count && found < max; i++) {
    int inputs = channel_count(ids[i], kAudioDevicePropertyScopeInput);
    int outputs = channel_count(ids[i], kAudioDevicePropertyScopeOutput);
    if (inputs == 0 && outputs == 0) {
      continue;
    }

    struct WhistleDevice* device = &out[found];
    memset(device, 0, sizeof(*device));
    device->id = ids[i];
    device->input_channels = inputs;
    device->output_channels = outputs;
    device->is_default_input = (ids[i] == default_in);
    device->is_default_output = (ids[i] == default_out);
    copy_cfstring(ids[i], kAudioObjectPropertyName,
                  kAudioObjectPropertyScopeGlobal, device->name,
                  sizeof(device->name));
    copy_cfstring(ids[i], kAudioDevicePropertyDeviceUID,
                  kAudioObjectPropertyScopeGlobal, device->uid,
                  sizeof(device->uid));
    Float64 rate = 0;
    get_property(ids[i], kAudioDevicePropertyNominalSampleRate,
                 kAudioObjectPropertyScopeGlobal, &rate, sizeof(rate));
    device->sample_rate = rate;
    found++;
  }
  free(ids);
  return found;
}

// A UID that is no longer present resolves to kAudioObjectUnknown, and the
// caller falls back to the system default rather than refusing to start.
// Unplugging an interface should not leave the app unable to make a sound.
static AudioDeviceID device_for_uid(const char* uid, bool input) {
  if (!uid || uid[0] == '\0') {
    return default_device(input);
  }
  struct WhistleDevice devices[WHISTLE_MAX_DEVICES];
  int count = whistle_list_devices(devices, WHISTLE_MAX_DEVICES);
  for (int i = 0; i < count; i++) {
    if (strcmp(devices[i].uid, uid) != 0) {
      continue;
    }
    int channels = input ? devices[i].input_channels
                         : devices[i].output_channels;
    if (channels > 0) {
      return devices[i].id;
    }
  }
  return default_device(input);
}

/* ------------------------------------------------------- the built-ins --- */

// A built-in device's data source, which is how one device ID covers both the
// speakers and whatever is in the headphone jack: the ID stays put and the
// source changes underneath it.  Spelled out rather than written as
// multi-character literals, which are implementation-defined.
static const UInt32 SOURCE_INTERNAL_SPEAKER = 0x6973706b;   // 'ispk'
static const UInt32 SOURCE_INTERNAL_MIC     = 0x696d6963;   // 'imic'

// 0 for a device with no data source property, which matches neither of the
// above -- so anything we cannot ask about is treated as not built in, and
// the player gets to play.  Guessing wrong in the other direction would mean
// refusing to run on hardware that is perfectly fine.
static UInt32 data_source(AudioDeviceID device,
                          AudioObjectPropertyScope scope) {
  UInt32 source = 0;
  get_property(device, kAudioDevicePropertyDataSource, scope, &source,
               sizeof(source));
  return source;
}

static bool is_builtin(AudioDeviceID device) {
  UInt32 transport = 0;
  if (get_property(device, kAudioDevicePropertyTransportType,
                   kAudioObjectPropertyScopeGlobal, &transport,
                   sizeof(transport)) != noErr) {
    return false;
  }
  return transport == kAudioDeviceTransportTypeBuiltIn;
}

// Headphones in the jack are the same device with a different data source,
// so this is false for them -- which is the point: headphones are the fix,
// not another case of the problem.
static bool is_builtin_speaker(AudioDeviceID device) {
  return is_builtin(device) &&
         data_source(device, kAudioDevicePropertyScopeOutput)
             == SOURCE_INTERNAL_SPEAKER;
}

static bool is_builtin_mic(AudioDeviceID device) {
  return is_builtin(device) &&
         data_source(device, kAudioDevicePropertyScopeInput)
             == SOURCE_INTERNAL_MIC;
}

void whistle_resolve_route(const struct WhistleConfig* config,
                           struct WhistleRoute* out) {
  memset(out, 0, sizeof(*out));

  AudioDeviceID input = device_for_uid(config->input_uid, true);
  AudioDeviceID output = device_for_uid(config->output_uid, false);

  out->have_input = input != kAudioObjectUnknown &&
                    channel_count(input, kAudioDevicePropertyScopeInput) > 0;
  out->have_output = output != kAudioObjectUnknown &&
                     channel_count(output, kAudioDevicePropertyScopeOutput) > 0;

  if (out->have_input) {
    copy_cfstring(input, kAudioObjectPropertyName,
                  kAudioObjectPropertyScopeGlobal, out->input_name,
                  sizeof(out->input_name));
  }
  if (out->have_output) {
    copy_cfstring(output, kAudioObjectPropertyName,
                  kAudioObjectPropertyScopeGlobal, out->output_name,
                  sizeof(out->output_name));
  }

  out->builtin_loop = out->have_input && out->have_output &&
                      is_builtin_mic(input) && is_builtin_speaker(output);
  out->usable = out->have_input && out->have_output && !out->builtin_loop;
}

static void (*devices_changed_callback)(void* context);
static void* devices_changed_context;

static OSStatus devices_changed(AudioObjectID object,
                                UInt32 count,
                                const AudioObjectPropertyAddress* addresses,
                                void* context) {
  (void)object;
  (void)count;
  (void)addresses;
  (void)context;
  if (devices_changed_callback) {
    devices_changed_callback(devices_changed_context);
  }
  return noErr;
}

void whistle_set_devices_changed_callback(void (*callback)(void* context),
                                          void* context) {
  static const AudioObjectPropertySelector watched[] = {
    kAudioHardwarePropertyDevices,
    kAudioHardwarePropertyDefaultInputDevice,
    kAudioHardwarePropertyDefaultOutputDevice,
  };
  static bool listening;

  devices_changed_callback = callback;
  devices_changed_context = context;
  if (listening || !callback) {
    return;
  }
  for (size_t i = 0; i < sizeof(watched) / sizeof(watched[0]); i++) {
    AudioObjectPropertyAddress address = {
      watched[i], kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMain };
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &address,
                                   devices_changed, NULL);
  }
  listening = true;
}

/* ----------------------------------------------------- device settings --- */

// The nominal sample rate belongs to the device, not to us: a device runs at
// one rate for everything using it, so changing it changes it for every other
// app on that device, and it stays changed after we quit unless we put it
// back.  So remember what a device was doing before we touched it, and
// restore it on the way out.  At most two devices are ever involved, one in
// each direction.
//
// The buffer size measures as per-client rather than shared (see
// request_buffer_frames), so restoring it is insurance rather than a
// correction -- cheap, and it costs nothing to be wrong about which.
static struct {
  AudioDeviceID device;
  bool had_rate;
  Float64 rate;
  bool had_frames;
  UInt32 frames;
} saved_settings[2];
static int saved_count;

static size_t saved_slot(AudioDeviceID device) {
  for (int i = 0; i < saved_count; i++) {
    if (saved_settings[i].device == device) {
      return (size_t)i;
    }
  }
  if (saved_count < (int)(sizeof(saved_settings) / sizeof(saved_settings[0]))) {
    int slot = saved_count++;
    memset(&saved_settings[slot], 0, sizeof(saved_settings[slot]));
    saved_settings[slot].device = device;
    return (size_t)slot;
  }
  return (size_t)0;   // unreachable: only ever an input and an output
}

// Best effort.  A device that will not run at the asked-for rate keeps its
// own, and we run at that instead -- the engine takes its rate as an
// argument, so there is nothing to break.
static void request_sample_rate(AudioDeviceID device, double rate) {
  if (rate <= 0) {
    return;
  }
  Float64 current = 0;
  if (get_property(device, kAudioDevicePropertyNominalSampleRate,
                   kAudioObjectPropertyScopeGlobal, &current, sizeof(current))
          == noErr &&
      current == (Float64)rate) {
    return;
  }
  AudioObjectPropertyAddress address = {
    kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain };
  Float64 wanted = (Float64)rate;
  if (AudioObjectSetPropertyData(device, &address, 0, NULL, sizeof(wanted),
                                 &wanted) != noErr) {
    return;
  }
  // Only after the set succeeded, and only the first time: a restart must not
  // overwrite the original with our own value from the previous run.
  size_t slot = saved_slot(device);
  if (!saved_settings[slot].had_rate && current > 0) {
    saved_settings[slot].had_rate = true;
    saved_settings[slot].rate = current;
  }
}

// The latency knob.  The device clamps to what it supports.
//
// This does *not* change the buffer size other apps get, which is worth
// stating because the obvious reading of the API is that it would, and
// because the command-line build's comments assumed it did.  Measured on
// macOS 26: with this code holding the default output device at 64 frames, a
// separate process reading kAudioDevicePropertyBufferFrameSize on the same
// device still gets 512.  The HAL keeps a size per client and adapts.  The
// device's hardware I/O cycle does get shorter, which costs a little power,
// but nothing else's latency or block size changes.
//
// Restored anyway, on the principle that what we changed we put back, and
// because the measurement is one OS version on one device.
static void request_buffer_frames(AudioDeviceID device, int frames) {
  if (frames <= 0) {
    return;
  }
  AudioValueRange range = { 0, 0 };
  if (get_property(device, kAudioDevicePropertyBufferFrameSizeRange,
                   kAudioObjectPropertyScopeGlobal, &range, sizeof(range))
      == noErr) {
    if (frames < (int)range.mMinimum) {
      frames = (int)range.mMinimum;
    }
    if (range.mMaximum > 0 && frames > (int)range.mMaximum) {
      frames = (int)range.mMaximum;
    }
  }
  UInt32 current = 0;
  bool have_current = get_property(device, kAudioDevicePropertyBufferFrameSize,
                                   kAudioObjectPropertyScopeGlobal, &current,
                                   sizeof(current)) == noErr && current > 0;
  if (have_current && current == (UInt32)frames) {
    return;
  }
  AudioObjectPropertyAddress address = {
    kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain };
  UInt32 wanted = (UInt32)frames;
  if (AudioObjectSetPropertyData(device, &address, 0, NULL, sizeof(wanted),
                                 &wanted) != noErr) {
    return;
  }
  size_t slot = saved_slot(device);
  if (!saved_settings[slot].had_frames && have_current) {
    saved_settings[slot].had_frames = true;
    saved_settings[slot].frames = current;
  }
}

// Hand the devices back the way we found them.  Called once the streams are
// closed, so the device is free to take the change.
static void restore_device_settings(void) {
  for (int i = 0; i < saved_count; i++) {
    AudioDeviceID device = saved_settings[i].device;
    if (saved_settings[i].had_frames) {
      AudioObjectPropertyAddress address = {
        kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain };
      UInt32 frames = saved_settings[i].frames;
      AudioObjectSetPropertyData(device, &address, 0, NULL, sizeof(frames),
                                 &frames);
    }
    if (saved_settings[i].had_rate) {
      AudioObjectPropertyAddress address = {
        kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain };
      Float64 rate = saved_settings[i].rate;
      AudioObjectSetPropertyData(device, &address, 0, NULL, sizeof(rate),
                                 &rate);
    }
  }
  saved_count = 0;
}

static int actual_buffer_frames(AudioDeviceID device) {
  UInt32 frames = 0;
  get_property(device, kAudioDevicePropertyBufferFrameSize,
               kAudioObjectPropertyScopeGlobal, &frames, sizeof(frames));
  return (int)frames;
}

static double actual_sample_rate(AudioDeviceID device) {
  Float64 rate = 0;
  get_property(device, kAudioDevicePropertyNominalSampleRate,
               kAudioObjectPropertyScopeGlobal, &rate, sizeof(rate));
  return (double)rate;
}

// What the hardware costs us before a sample reaches the callback, or after
// it leaves: the converter's own latency plus the safety offset plus the
// buffer.  This is the floor the README's measurements ran into.
static double hardware_latency_ms(AudioDeviceID device,
                                  AudioObjectPropertyScope scope,
                                  int buffer_frames,
                                  double rate) {
  if (rate <= 0) {
    return 0;
  }
  UInt32 latency = 0;
  UInt32 safety = 0;
  get_property(device, kAudioDevicePropertyLatency, scope, &latency,
               sizeof(latency));
  get_property(device, kAudioDevicePropertySafetyOffset, scope, &safety,
               sizeof(safety));
  return 1000.0 * (latency + safety + buffer_frames) / rate;
}

/* ---------------------------------------------------------------- ring --- */

// Single producer (the input device's thread), single consumer (the output
// device's).  Split mode only.
#define RING_FRAMES 16384u   // power of two, for the mask

static float ring[RING_FRAMES];
static _Atomic unsigned ring_write;
static _Atomic unsigned ring_read;

static unsigned ring_fill(void) {
  unsigned write = atomic_load_explicit(&ring_write, memory_order_acquire);
  unsigned read = atomic_load_explicit(&ring_read, memory_order_relaxed);
  return write - read;
}

static void ring_reset(void) {
  atomic_store_explicit(&ring_write, 0, memory_order_relaxed);
  atomic_store_explicit(&ring_read, 0, memory_order_relaxed);
  memset(ring, 0, sizeof(ring));
}

static void ring_push(const float* samples, int frames) {
  unsigned write = atomic_load_explicit(&ring_write, memory_order_relaxed);
  unsigned read = atomic_load_explicit(&ring_read, memory_order_acquire);
  unsigned space = RING_FRAMES - (write - read) - 1;
  if ((unsigned)frames > space) {
    // The consumer has stalled.  Drop this block rather than the whole
    // history: the detector recovers within an analysis window.
    whistle_note_dropouts(1);
    return;
  }
  for (int i = 0; i < frames; i++) {
    ring[(write + (unsigned)i) & (RING_FRAMES - 1)] = samples[i];
  }
  atomic_store_explicit(&ring_write, write + (unsigned)frames,
                        memory_order_release);
}

static void ring_pop(float* samples, int frames, unsigned target_fill) {
  unsigned read = atomic_load_explicit(&ring_read, memory_order_relaxed);
  unsigned write = atomic_load_explicit(&ring_write, memory_order_acquire);
  unsigned available = write - read;

  // Two clocks that drift means the backlog wanders.  Left alone it ends up
  // either empty or arbitrarily deep, so pull it back towards the target.
  if (available > target_fill * 3 && target_fill > 0) {
    unsigned skip = available - target_fill;
    read += skip;
    available -= skip;
    whistle_note_dropouts(1);
  }

  for (int i = 0; i < frames; i++) {
    if ((unsigned)i < available) {
      samples[i] = ring[(read + (unsigned)i) & (RING_FRAMES - 1)];
    } else {
      samples[i] = 0;   // input has not caught up yet
    }
  }
  if ((unsigned)frames > available) {
    whistle_note_dropouts(1);
    read += available;
  } else {
    read += (unsigned)frames;
  }
  atomic_store_explicit(&ring_read, read, memory_order_release);
}

/* -------------------------------------------------------------- stream --- */

static AudioComponentInstance output_unit;
static AudioComponentInstance input_unit;   // split mode only
static bool stream_running;
static bool stream_split;

static int stream_input_channels;
static int stream_output_channels;
static unsigned stream_target_fill;

static AudioBufferList* input_list;   // for AudioUnitRender
static float* input_scratch;          // what AudioUnitRender writes into
static float* left_scratch;
static float* right_scratch;
static UInt32 scratch_frames;

static void free_scratch(void) {
  free(input_list);
  input_list = NULL;
  free(input_scratch);
  input_scratch = NULL;
  free(left_scratch);
  left_scratch = NULL;
  free(right_scratch);
  right_scratch = NULL;
  scratch_frames = 0;
}

static bool allocate_scratch(UInt32 frames, int input_channels) {
  free_scratch();
  size_t list_size = sizeof(AudioBufferList) +
      sizeof(AudioBuffer) * (size_t)(input_channels > 0 ? input_channels - 1 : 0);
  input_list = (AudioBufferList*)calloc(1, list_size);
  input_scratch = (float*)calloc(frames * (size_t)(input_channels > 0
                                                       ? input_channels : 1),
                                 sizeof(float));
  left_scratch = (float*)calloc(frames, sizeof(float));
  right_scratch = (float*)calloc(frames, sizeof(float));
  if (!input_list || !input_scratch || !left_scratch || !right_scratch) {
    free_scratch();
    fail("out of memory", noErr);
    return false;
  }
  scratch_frames = frames;
  return true;
}

// Points the buffer list at the scratch memory for this many frames.  AUHAL
// wants the sizes right on every call, and it may hand back fewer frames
// than asked for.
static void prepare_input_list(UInt32 frames) {
  input_list->mNumberBuffers = (UInt32)stream_input_channels;
  for (int channel = 0; channel < stream_input_channels; channel++) {
    input_list->mBuffers[channel].mNumberChannels = 1;
    input_list->mBuffers[channel].mDataByteSize = frames * sizeof(float);
    input_list->mBuffers[channel].mData =
        input_scratch + (size_t)channel * scratch_frames;
  }
}

// Only channel 0 carries the whistle.  Everything else the device offers is
// ignored rather than mixed in, so a stereo interface with something else
// plugged into input 2 does not confuse the detector.
static const float* rendered_input(void) {
  return (const float*)input_list->mBuffers[0].mData;
}

// Channel 0 gets left, channel 1 gets right, and a device with more than two
// repeats the pair rather than leaving the extras silent.  A mono output
// device gets the average, which is exactly the mono signal -- the two
// channels are mid+side and mid-side, so nothing cancels on the way down.
static void fan_out(AudioBufferList* io, const float* left, const float* right,
                    UInt32 frames) {
  bool mono_device = (io->mNumberBuffers == 1 &&
                      io->mBuffers[0].mNumberChannels == 1);

  for (UInt32 buffer = 0; buffer < io->mNumberBuffers; buffer++) {
    float* out = (float*)io->mBuffers[buffer].mData;
    if (!out) {
      continue;
    }
    UInt32 channels = io->mBuffers[buffer].mNumberChannels;
    if (channels == 1) {
      // Non-interleaved, the usual case: one channel per buffer.
      const float* source = (buffer & 1) ? right : left;
      if (mono_device) {
        for (UInt32 i = 0; i < frames; i++) {
          out[i] = 0.5f * (left[i] + right[i]);
        }
      } else {
        memcpy(out, source, frames * sizeof(float));
      }
    } else {
      // Interleaved, which AUHAL should not hand us -- but a device that does
      // would otherwise be written past the end of.
      for (UInt32 i = 0; i < frames; i++) {
        for (UInt32 c = 0; c < channels; c++) {
          out[i * channels + c] = (c & 1) ? right[i] : left[i];
        }
      }
    }
  }
}

static double expected_sample_time = -1;
static int gap_warmup;

static void note_gap(const AudioTimeStamp* timestamp, UInt32 frames) {
  if (!timestamp || !(timestamp->mFlags & kAudioTimeStampSampleTimeValid)) {
    return;
  }
  // A device settling into its first few callbacks skips around, and counting
  // that would mean every launch opens showing a dropout it did not have.
  if (gap_warmup > 0) {
    gap_warmup--;
  } else if (expected_sample_time >= 0 &&
             timestamp->mSampleTime > expected_sample_time + 0.5) {
    whistle_note_xrun();
  }
  expected_sample_time = timestamp->mSampleTime + frames;
}

// Duplex: the input is available right here, so nothing is buffered between
// hearing a sample and answering it.
static OSStatus duplex_render(void* context,
                              AudioUnitRenderActionFlags* flags,
                              const AudioTimeStamp* timestamp,
                              UInt32 bus,
                              UInt32 frames,
                              AudioBufferList* io) {
  (void)context;
  (void)bus;
  (void)flags;
  if (frames > scratch_frames) {
    return kAudioUnitErr_TooManyFramesToProcess;
  }

  prepare_input_list(frames);
  AudioUnitRenderActionFlags input_flags = 0;
  OSStatus status = AudioUnitRender(output_unit, &input_flags, timestamp, 1,
                                    frames, input_list);
  const float* input = status == noErr ? rendered_input() : NULL;
  if (status != noErr) {
    whistle_note_xrun();
  }

  whistle_engine_process(input, left_scratch, right_scratch, (int)frames);
  fan_out(io, left_scratch, right_scratch, frames);
  note_gap(timestamp, frames);
  return noErr;
}

// Split: the input device's thread drops samples into the ring...
static OSStatus split_input(void* context,
                            AudioUnitRenderActionFlags* flags,
                            const AudioTimeStamp* timestamp,
                            UInt32 bus,
                            UInt32 frames,
                            AudioBufferList* io) {
  (void)context;
  (void)io;
  if (frames > scratch_frames) {
    return noErr;
  }
  prepare_input_list(frames);
  if (AudioUnitRender(input_unit, flags, timestamp, bus, frames, input_list)
      != noErr) {
    whistle_note_xrun();
    return noErr;
  }
  ring_push(rendered_input(), (int)frames);
  return noErr;
}

// ...and the output device's thread takes them out.
static OSStatus split_render(void* context,
                             AudioUnitRenderActionFlags* flags,
                             const AudioTimeStamp* timestamp,
                             UInt32 bus,
                             UInt32 frames,
                             AudioBufferList* io) {
  (void)context;
  (void)bus;
  (void)flags;
  if (frames > scratch_frames) {
    return kAudioUnitErr_TooManyFramesToProcess;
  }
  // Into the left buffer and then out of it again: whistle_engine_process
  // reads in[i] before writing left[i] and never looks back, so the two can
  // be the same buffer.  They have to be something other than
  // `input_scratch`, which is where the *input* device's thread is rendering
  // right now -- borrowing that here would be a data race on it.
  ring_pop(left_scratch, (int)frames, stream_target_fill);
  whistle_engine_process(left_scratch, left_scratch, right_scratch,
                         (int)frames);
  fan_out(io, left_scratch, right_scratch, frames);
  note_gap(timestamp, frames);
  return noErr;
}

/* ---------------------------------------------------------- unit setup --- */

static AudioComponentInstance new_halt_unit(void) {
  AudioComponentDescription description = {
    .componentType = kAudioUnitType_Output,
    .componentSubType = kAudioUnitSubType_HALOutput,
    .componentManufacturer = kAudioUnitManufacturer_Apple,
  };
  AudioComponent component = AudioComponentFindNext(NULL, &description);
  if (!component) {
    fail("no CoreAudio output unit available", noErr);
    return NULL;
  }
  AudioComponentInstance unit = NULL;
  OSStatus status = AudioComponentInstanceNew(component, &unit);
  if (status != noErr) {
    fail("could not create the audio unit", status);
    return NULL;
  }
  return unit;
}

static bool enable_io(AudioComponentInstance unit, bool input, bool output) {
  UInt32 on = 1;
  UInt32 off = 0;
  OSStatus status = AudioUnitSetProperty(
      unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1,
      input ? &on : &off, sizeof(UInt32));
  if (status != noErr) {
    fail("could not enable audio input", status);
    return false;
  }
  status = AudioUnitSetProperty(
      unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0,
      output ? &on : &off, sizeof(UInt32));
  if (status != noErr) {
    fail("could not enable audio output", status);
    return false;
  }
  return true;
}

static bool set_device(AudioComponentInstance unit, AudioDeviceID device) {
  OSStatus status = AudioUnitSetProperty(
      unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
      &device, sizeof(device));
  if (status != noErr) {
    fail("could not select that audio device", status);
    return false;
  }
  return true;
}

// Non-interleaved float32, which is what AUHAL speaks natively, so nothing
// has to be de-interleaved on the audio thread.
static AudioStreamBasicDescription client_format(double rate, int channels) {
  AudioStreamBasicDescription format = {
    .mSampleRate = rate,
    .mFormatID = kAudioFormatLinearPCM,
    .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked |
                    kAudioFormatFlagIsNonInterleaved,
    .mBitsPerChannel = 32,
    .mChannelsPerFrame = (UInt32)channels,
    .mFramesPerPacket = 1,
    .mBytesPerFrame = sizeof(float),
    .mBytesPerPacket = sizeof(float),
  };
  return format;
}

static bool set_max_frames(AudioComponentInstance unit, UInt32 frames) {
  OSStatus status = AudioUnitSetProperty(
      unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global,
      0, &frames, sizeof(frames));
  if (status != noErr) {
    fail("could not set the audio block size", status);
    return false;
  }
  return true;
}

static void teardown(void) {
  if (input_unit) {
    AudioOutputUnitStop(input_unit);
    AudioUnitUninitialize(input_unit);
    AudioComponentInstanceDispose(input_unit);
    input_unit = NULL;
  }
  if (output_unit) {
    AudioOutputUnitStop(output_unit);
    AudioUnitUninitialize(output_unit);
    AudioComponentInstanceDispose(output_unit);
    output_unit = NULL;
  }
  free_scratch();
  // After the units are gone, so the device will accept the change.
  restore_device_settings();
  stream_running = false;
}

void whistle_stop(void) {
  if (!stream_running && !output_unit && !input_unit) {
    return;
  }
  teardown();
  whistle_publish_stream(false, 0, 0, 0, 0, "", "", false);
}

bool whistle_start(const struct WhistleConfig* config) {
  whistle_stop();
  last_error[0] = '\0';

  AudioDeviceID input_device = device_for_uid(config->input_uid, true);
  AudioDeviceID output_device = device_for_uid(config->output_uid, false);
  if (input_device == kAudioObjectUnknown) {
    fail("no audio input device is available", noErr);
    return false;
  }
  if (output_device == kAudioObjectUnknown) {
    fail("no audio output device is available", noErr);
    return false;
  }
  // See WhistleRoute.builtin_loop.  Checked here as well as in the UI so that
  // there is one place this is decided, whatever route the call came in by.
  if (is_builtin_mic(input_device) && is_builtin_speaker(output_device)) {
    fail("this Mac's microphone and its speakers cannot be used together: "
         "the speakers are pointed at the microphone, so the synth would "
         "hear itself. Connect headphones or an audio interface.", noErr);
    return false;
  }

  stream_split = (input_device != output_device);
  stream_input_channels = channel_count(input_device,
                                        kAudioDevicePropertyScopeInput);
  stream_output_channels = channel_count(output_device,
                                         kAudioDevicePropertyScopeOutput);
  if (stream_input_channels <= 0) {
    fail("that input device has no input channels", noErr);
    return false;
  }
  if (stream_output_channels <= 0) {
    fail("that output device has no output channels", noErr);
    return false;
  }

  request_sample_rate(output_device, config->sample_rate);
  request_buffer_frames(output_device, config->buffer_frames);
  if (stream_split) {
    // Ask the input device for the same rate, so AUHAL's converter has
    // nothing to do.  If it refuses, the converter handles the difference.
    request_sample_rate(input_device, config->sample_rate > 0
                                          ? config->sample_rate
                                          : actual_sample_rate(output_device));
    request_buffer_frames(input_device, config->buffer_frames);
  }

  // The output device sets the pace: it is the one that must never be late.
  double rate = actual_sample_rate(output_device);
  if (rate <= 0) {
    fail("could not read the output device's sample rate", noErr);
    return false;
  }
  int buffer_frames = actual_buffer_frames(output_device);
  if (buffer_frames <= 0) {
    buffer_frames = 512;
  }

  UInt32 max_frames = (UInt32)(buffer_frames * 4);
  if (stream_split) {
    int input_buffer = actual_buffer_frames(input_device);
    if (input_buffer * 4 > (int)max_frames) {
      max_frames = (UInt32)(input_buffer * 4);
    }
  }
  if (max_frames < 512) {
    max_frames = 512;
  }
  if (!allocate_scratch(max_frames, stream_input_channels)) {
    return false;
  }

  // Aim to keep about two output blocks in the ring: enough that ordinary
  // jitter between the two clocks does not empty it, little enough that the
  // added latency stays under a few milliseconds.
  stream_target_fill = (unsigned)(buffer_frames * 2);
  expected_sample_time = -1;
  gap_warmup = 16;
  ring_reset();

  AudioStreamBasicDescription input_client =
      client_format(rate, stream_input_channels);
  AudioStreamBasicDescription output_client =
      client_format(rate, stream_output_channels);

  output_unit = new_halt_unit();
  if (!output_unit) {
    teardown();
    return false;
  }

  OSStatus status;
  if (!stream_split) {
    if (!enable_io(output_unit, true, true) ||
        !set_device(output_unit, output_device) ||
        !set_max_frames(output_unit, max_frames)) {
      teardown();
      return false;
    }
    // Bus 1's output scope is what AudioUnitRender hands us.
    status = AudioUnitSetProperty(output_unit, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Output, 1, &input_client,
                                  sizeof(input_client));
    if (status != noErr) {
      fail("that device would not accept the input format", status);
      teardown();
      return false;
    }
    status = AudioUnitSetProperty(output_unit, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input, 0, &output_client,
                                  sizeof(output_client));
    if (status != noErr) {
      fail("that device would not accept the output format", status);
      teardown();
      return false;
    }
    AURenderCallbackStruct callback = { duplex_render, NULL };
    status = AudioUnitSetProperty(output_unit,
                                  kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input, 0, &callback,
                                  sizeof(callback));
    if (status != noErr) {
      fail("could not install the audio callback", status);
      teardown();
      return false;
    }
  } else {
    if (!enable_io(output_unit, false, true) ||
        !set_device(output_unit, output_device) ||
        !set_max_frames(output_unit, max_frames)) {
      teardown();
      return false;
    }
    status = AudioUnitSetProperty(output_unit, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input, 0, &output_client,
                                  sizeof(output_client));
    if (status != noErr) {
      fail("that output device would not accept the output format", status);
      teardown();
      return false;
    }
    AURenderCallbackStruct callback = { split_render, NULL };
    status = AudioUnitSetProperty(output_unit,
                                  kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input, 0, &callback,
                                  sizeof(callback));
    if (status != noErr) {
      fail("could not install the audio callback", status);
      teardown();
      return false;
    }

    input_unit = new_halt_unit();
    if (!input_unit ||
        !enable_io(input_unit, true, false) ||
        !set_device(input_unit, input_device) ||
        !set_max_frames(input_unit, max_frames)) {
      teardown();
      return false;
    }
    status = AudioUnitSetProperty(input_unit, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Output, 1, &input_client,
                                  sizeof(input_client));
    if (status != noErr) {
      fail("that input device would not accept the input format", status);
      teardown();
      return false;
    }
    AURenderCallbackStruct callback_in = { split_input, NULL };
    status = AudioUnitSetProperty(input_unit,
                                  kAudioOutputUnitProperty_SetInputCallback,
                                  kAudioUnitScope_Global, 0, &callback_in,
                                  sizeof(callback_in));
    if (status != noErr) {
      fail("could not install the input callback", status);
      teardown();
      return false;
    }
  }

  // Everything the audio thread will touch has to exist before it runs.
  whistle_engine_prepare(rate);

  if (input_unit) {
    status = AudioUnitInitialize(input_unit);
    if (status != noErr) {
      fail("could not open the input device", status);
      teardown();
      return false;
    }
  }
  status = AudioUnitInitialize(output_unit);
  if (status != noErr) {
    fail("could not open the output device", status);
    teardown();
    return false;
  }

  if (input_unit) {
    status = AudioOutputUnitStart(input_unit);
    if (status != noErr) {
      fail("could not start the input device", status);
      teardown();
      return false;
    }
    // Let the input get a block or two ahead before the output starts asking
    // for samples.  Otherwise the first few callbacks find the ring empty and
    // report underruns that are really just startup, which matters because
    // the dropout count is how a player decides whether to raise the buffer
    // size.  Bounded, so a device that never delivers does not hang the UI.
    for (int wait = 0; wait < 200 && ring_fill() < stream_target_fill; wait++) {
      usleep(1000);
    }
  }
  status = AudioOutputUnitStart(output_unit);
  if (status != noErr) {
    fail("could not start the output device", status);
    teardown();
    return false;
  }
  stream_running = true;

  char input_name[WHISTLE_NAME_MAX];
  char output_name[WHISTLE_NAME_MAX];
  copy_cfstring(input_device, kAudioObjectPropertyName,
                kAudioObjectPropertyScopeGlobal, input_name,
                sizeof(input_name));
  copy_cfstring(output_device, kAudioObjectPropertyName,
                kAudioObjectPropertyScopeGlobal, output_name,
                sizeof(output_name));

  double input_latency = hardware_latency_ms(
      input_device, kAudioDevicePropertyScopeInput,
      stream_split ? actual_buffer_frames(input_device) : buffer_frames, rate);
  double output_latency = hardware_latency_ms(
      output_device, kAudioDevicePropertyScopeOutput, buffer_frames, rate);
  if (stream_split) {
    // The ring is real latency and should be counted, not hidden.
    input_latency += 1000.0 * stream_target_fill / rate;
  }

  whistle_publish_stream(true, rate, buffer_frames, input_latency,
                         output_latency, input_name, output_name,
                         stream_split);
  return true;
}
