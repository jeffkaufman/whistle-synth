// What the CoreAudio layer needs from the engine layer.  Not for Swift.
#ifndef WHISTLE_INTERNAL_H
#define WHISTLE_INTERNAL_H

#include <stdbool.h>

// Builds the engine for a stream about to start, at the rate the device
// actually settled on, and applies whatever the UI has published so far.
// Called with no audio running, from whichever thread drives the stream's
// lifecycle -- one thread only, which the app guarantees with a serial
// queue.  See AudioLifecycle.
void whistle_engine_prepare(double sample_rate);

// One block, mono in to stereo out, on the audio thread.  Realtime safe.
// `in` may be NULL, which is read as silence, and may alias `left`.
void whistle_engine_process(const float* in, float* left, float* right,
                            int frames);

// Counters the UI shows.  Safe from the audio thread.
void whistle_note_xrun(void);
void whistle_note_dropouts(int count);

// Stream facts, published for whistle_status.  Called from the lifecycle
// thread with the stream stopped; read from the main thread at any time,
// which the generation counter in the implementation is what makes safe.
void whistle_publish_stream(bool running,
                            double sample_rate,
                            int buffer_frames,
                            double input_latency_ms,
                            double output_latency_ms,
                            const char* input_name,
                            const char* output_name,
                            bool split_devices);

#endif  // WHISTLE_INTERNAL_H
