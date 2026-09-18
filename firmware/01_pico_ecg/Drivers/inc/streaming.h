#ifndef STREAMING_H
#define STREAMING_H

#include <stdint.h>
#include <stdbool.h>

// Streaming buffer for ECG sample data from Core 1 -> Core 0 (USB CDC).
// Core 1 writes samples each cycle, Core 0 reads and sends binary frames.
//
// Frame format (identical to pico_ecg_conduction_model):
//   [0xAA 0x55] [9 x float32 LE] [0x55 0xAA]
//   Total = 2 + 36 + 2 = 40 bytes per frame

#define STREAM_NUM_CHANNELS 9
#define STREAM_BUF_SAMPLES  8192

typedef struct {
    float ch[STREAM_NUM_CHANNELS];
} stream_sample_t;

typedef struct {
    stream_sample_t buf[STREAM_BUF_SAMPLES];
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;
} stream_buffer_t;

extern stream_buffer_t stream_buffer;
extern volatile bool stream_mode_active;

static inline bool stream_buffer_has_data(void) {
    return stream_buffer.write_idx != stream_buffer.read_idx;
}

static inline void stream_buffer_add(const float* channels) {
    uint32_t next = (stream_buffer.write_idx + 1) % STREAM_BUF_SAMPLES;
    if (next == stream_buffer.read_idx) return; // buffer full – drop sample
    for (int i = 0; i < STREAM_NUM_CHANNELS; i++)
        stream_buffer.buf[stream_buffer.write_idx].ch[i] = channels[i];
    stream_buffer.write_idx = next;
}

static inline bool stream_buffer_read(float* out) {
    if (stream_buffer.write_idx == stream_buffer.read_idx) return false;
    for (int i = 0; i < STREAM_NUM_CHANNELS; i++)
        out[i] = stream_buffer.buf[stream_buffer.read_idx].ch[i];
    stream_buffer.read_idx = (stream_buffer.read_idx + 1) % STREAM_BUF_SAMPLES;
    return true;
}

#endif // STREAMING_H
