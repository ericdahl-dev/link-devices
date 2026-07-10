#pragma once
// Pure beat stamper (P4-032 Tier 3). Assigns each captured audio block the
// beatsAtBufferBegin that LinkAudioSink::BufferHandle::commit() needs.
//
// The problem it owns: Live reassembles the stream from these stamps, so they
// must be CONTINUOUS in beat time -- consecutive blocks must abut exactly, or
// the receiver hears a glitch. But the measurement we have (the session beat
// at the moment the I2S DMA read returns) jitters with task scheduling, and
// the I2S crystal drifts against the Link timeline. Policy:
//
//   - first block: anchor so the block ENDS at the measured beat (the DMA
//     read returns at buffer end),
//   - after that: advance by exactly frames/rate * beats-per-second and
//     ignore measured jitter,
//   - unless |measured - predicted| exceeds the resync threshold (transport
//     relocate, session takeover): snap back to measured -- one audible
//     glitch beats a permanent offset.
//
// No SDK/ESP dependency -- host-tested in test/test_beat_stamper.c.
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Predicted-vs-measured divergence (in beats) that forces a resync.
// 0.25 beat = 125ms at 120 BPM: far beyond scheduling jitter, well under a
// musically meaningful relocate.
#define BEAT_STAMPER_RESYNC_BEATS 0.25

typedef struct {
    bool   anchored;
    double next_begin;   // where the NEXT block starts, in session beats
} BeatStamper;

void beat_stamper_reset(BeatStamper* s);

// Stamp one block. `measured_end_beats` = session beat observed when the
// block's DMA read returned (i.e. at the block's END); `bps` = tempo/60;
// `frames`/`rate` describe the block. Returns beatsAtBufferBegin.
double beat_stamper_stamp(BeatStamper* s, double measured_end_beats,
                          double bps, uint32_t frames, uint32_t rate);

#ifdef __cplusplus
}
#endif
