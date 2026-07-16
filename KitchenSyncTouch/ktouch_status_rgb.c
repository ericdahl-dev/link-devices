#include "ktouch_status_rgb.h"
#include <math.h>

// The flash occupies the first slice of each beat: bright then dark, a hammer-fall rather
// than a throb. 0.14 of a beat is ~67 ms at 125 BPM -- readable, not a smear at fast tempi.
#define FLASH_FRACTION 0.14f

StatusRgb ktouch_status_rgb(TransportLaunchState transport, bool link_locked,
                            float beats, int quantum) {
    const StatusRgb dark = { 0, 0, 0 };

    // Stopped says nothing, even while the clock free-runs -- the light reflects transport.
    if (transport == TL_STOPPED) return dark;

    const int   whole = (int)floorf(beats);
    const float frac  = beats - (float)whole;
    if (frac >= FLASH_FRACTION) return dark;          // between beats: dark

    if (quantum < 1) quantum = 1;
    const bool downbeat = (whole % quantum) == 0;      // bar-1 "one"

    if (transport == TL_ARMED) {
        // Cocked hammer: amber blink at the beat rate, brighter on the downbeat. Redder
        // than green so it reads amber (a warning-to-come), not the running green/cyan.
        const StatusRgb amber      = { 60, 20, 0 };
        const StatusRgb amber_down = { 110, 38, 0 };
        return downbeat ? amber_down : amber;
    }

    // TL_RUNNING: green free-run, cyan when phase-locked to Link. The downbeat is a brighter
    // accent of the SAME hue, so the "one" reads without masking the free/Link distinction.
    if (link_locked) {
        const StatusRgb cyan      = { 0, 32, 38 };
        const StatusRgb cyan_down = { 0, 70, 82 };
        return downbeat ? cyan_down : cyan;
    }
    // Pure green -- NO blue, so it never reads as a dim cyan. The blue channel is what
    // distinguishes "locked" at a glance, so free-run must keep it at zero.
    const StatusRgb green      = { 0, 40, 0 };
    const StatusRgb green_down = { 0, 90, 0 };
    return downbeat ? green_down : green;
}
