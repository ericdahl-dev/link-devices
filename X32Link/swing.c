#include "swing.h"
#include <math.h>

double swing_warp(double beats, int swing_mbeats) {
    if (swing_mbeats <= 0) return beats;   // straight

    double ratio = 0.5 + (double)swing_mbeats / 1000.0;   // (0.5, 0.75]
    if (ratio > 0.75) ratio = 0.75;                       // config caps at 250, but be safe

    double n = floor(beats);
    double f = beats - n;   // fraction within the beat, [0, 1)

    // Piecewise-linear map: the first eighth [0, ratio) real -> [0, 0.5) grid
    // (stretched), the second eighth [ratio, 1) real -> [0.5, 1) grid (compressed).
    // Continuous at f == ratio (both give 0.5) and f == 0/1 (fixed points).
    double w = (f < ratio) ? f * 0.5 / ratio
                           : 0.5 + (f - ratio) * 0.5 / (1.0 - ratio);
    return n + w;
}
