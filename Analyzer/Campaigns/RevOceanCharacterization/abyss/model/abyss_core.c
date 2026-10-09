/* Core loops of the structural Abyss model (analysis2/model_structural/abyss_model.py).
 *
 *     cc -O2 -shared -fPIC -ffp-contract=off -o abyss_core.dylib abyss_core.c
 *
 * 1. abyss_network: the campaign's base network (Analyzer/Campaigns/RevOceanCharacterization/network_model.c,
 *    copied loop for loop) with ONE change: the 32 line oscillators do not count from the instance's first
 *    sample but from a given step count (`osc_count` = at-lines index of frame 0 minus the oscillator origin).
 *    The single-precision accumulators repeat exactly after OSC_PERIOD steps once OSC_SETTLE steps have been
 *    done (checked by abyss_model.check_oscillator_period), so large counts are reduced before stepping.
 * 2. abyss_accumulators: the accumulator states after a number of steps (for that check).
 * 3. abyss_comb: y[n] += g * y[n - delay] in place (the 1 s recirculation of the octave-up voice).
 */
#include <math.h>
#include <stdlib.h>

#define LINES 16
#define GROUPS 2
#define BUFFER 32768
#define MASK (BUFFER - 1)
#define MAX_SECTIONS 4
#define OSC_PERIOD 13159175L
#define OSC_SETTLE 26000000L

static const float TWO_PI = 6.283185307179586f;
static const double HALF_PI = 1.5707963267948966;
static const double HALF_PI_SINGLE = (double)1.5707963267948966f;

static float oscillator_sine(float theta)
{
    const double quadrant = nearbyint((double)theta / HALF_PI);
    return (float)sin((double)theta - quadrant * (HALF_PI_SINGLE - HALF_PI));
}

static long reduced(long count, int reduce)
{
    if (reduce && count > OSC_SETTLE + OSC_PERIOD)
        count = OSC_SETTLE + (count - OSC_SETTLE) % OSC_PERIOD;
    return count;
}

/* theta[g][k] after `count` steps (wrap test, then addition, per step); phase index 2 * line + group */
static void start_accumulators(long count, const float *phase, float increment, float theta[GROUPS][LINES], int reduce)
{
    count = reduced(count, reduce);
    for (int g = 0; g < GROUPS; ++g)
        for (int k = 0; k < LINES; ++k) {
            float t = phase[2 * k + g];
            for (long m = 0; m < count; ++m) {
                if (t >= TWO_PI)
                    t -= TWO_PI;
                t += increment;
            }
            theta[g][k] = t;
        }
}

void abyss_accumulators(long count, const float *phase, float increment, int reduce, float *out)
{
    float theta[GROUPS][LINES];
    start_accumulators(count, phase, increment, theta, reduce);
    for (int g = 0; g < GROUPS; ++g)
        for (int k = 0; k < LINES; ++k)
            out[2 * k + g] = theta[g][k];
}

static float step_accumulator(float *theta, float increment)
{
    float t = *theta;
    if (t >= TWO_PI)
        t -= TWO_PI;
    *theta = t + increment;
    return t;
}

/* Same arguments as network_core of network_model.c, except: `osc_count` steps of the oscillators have been
 * done before frame 0 (negative: the oscillators start that many frames into the render; before that they
 * are held at their start phases, which is not a measured behaviour), and there is no `lengths` hook. */
void abyss_network(long frames, long osc_count, const double *input, const int *length, float depth,
                   const float *phase, float increment, const double *attenuation, const double *own,
                   const double *cross, const double *matrix, const double *kernel, int sections,
                   const double *mix, int rows, double *output)
{
    double *buffer = calloc((size_t)GROUPS * LINES * BUFFER, sizeof(double));
    double state[GROUPS][LINES][MAX_SECTIONS][2] = {{{{0.0}}}};
    float theta[GROUPS][LINES];
    if (sections > MAX_SECTIONS)
        sections = MAX_SECTIONS;
    start_accumulators(osc_count > 0 ? osc_count : 0, phase, increment, theta, 1);
    for (long m = 0; m < frames; ++m) {
        const int running = (osc_count + m) >= 0;
        for (int g = 0; g < GROUPS; ++g) {
            double a[LINES];
            for (int k = 0; k < LINES; ++k) {
                float t;
                if (running)
                    t = step_accumulator(&theta[g][k], increment);
                else
                    t = theta[g][k];
                const float modulation = depth * oscillator_sine(t);
                const float single = (float)length[g * LINES + k] + modulation;
                const double line_length = (double)single;
                const long whole = (long)floor(line_length);
                const double fraction = line_length - (double)whole;
                const double *line = buffer + (size_t)(g * LINES + k) * BUFFER;
                a[k] = attenuation[g * LINES + k]
                       * ((1.0 - fraction) * line[(m - whole) & MASK] + fraction * line[(m - whole - 1) & MASK]);
            }
            for (int r = 0; r < rows; ++r) {
                double sum = 0.0;
                for (int k = 0; k < LINES; ++k)
                    sum += mix[r * LINES + k] * a[k];
                output[((size_t)m * rows + r) * GROUPS + g] = sum;
            }
            for (int j = 0; j < LINES; ++j) {
                double x = 0.0;
                for (int k = 0; k < LINES; ++k)
                    x += matrix[j * LINES + k] * a[k];
                for (int section = 0; section < sections; ++section) {
                    const double *c = kernel + 5 * section;
                    double *s = state[g][j][section];
                    const double y = c[0] * x + s[0];
                    s[0] = c[1] * x - c[3] * y + s[1];
                    s[1] = c[2] * x - c[4] * y;
                    x = y;
                }
                buffer[(size_t)(g * LINES + j) * BUFFER + (m & MASK)] =
                    own[j] * input[2 * m + g] + cross[j] * input[2 * m + 1 - g] + x;
            }
        }
    }
    free(buffer);
}

/* y[n] += gain * y[n - delay], n = delay .. frames - 1, in place, for `channels` interleaved channels */
void abyss_comb(long frames, int channels, long delay, double gain, double *y)
{
    for (long n = delay; n < frames; ++n)
        for (int c = 0; c < channels; ++c)
            y[n * channels + c] += gain * y[(n - delay) * channels + c];
}

/* 4. abyss_reader: the two-grain reader behind a reversed stream.
 *    A single-precision phasor p (p += inc; if (p >= 1) p -= 1) has the value `start` at absolute sample `reset` and is
 *    stepped once per sample. Two taps with phases p and p + 0.5 (wrapped); a tap with phase q has the window
 *    sin(pi q) and reads the stream `delay` samples back with linear interpolation:
 *        delay = dmin + W q          (direction 0: delay grows, the stream is read slower than real time)
 *        delay = dmin + W (1 - q)    (direction 1: delay shrinks, the stream is read faster)
 *    y[i], out[i] belong to absolute sample first + i; `shift` is added to the read position (for fitting).
 *    The window is sin(pi (wscale q + woff)) where that argument lies in 0..1, else 0.
 *    Samples before `reset` give 0. */
void abyss_reader(long frames, long first, const double *y, long reset, float start, float inc, double W, double dmin,
                  int direction, double shift, double wscale, double woff, double *out)
{
    const double PI = 3.14159265358979323846;
    float p = start;
    long n = reset;
    for (; n < first; ++n) {
        p += inc;
        if (p >= 1.0f)
            p -= 1.0f;
    }
    for (long i = 0; i < frames; ++i)
        out[i] = 0.0;
    for (; n < first + frames; ++n) {
        const long i = n - first;
        if (i >= 0) {
            double acc = 0.0;
            for (int tap = 0; tap < 2; ++tap) {
                float q = p;
                if (tap) {
                    q = p + 0.5f;
                    if (q >= 1.0f)
                        q -= 1.0f;
                }
                const double phase = (double)q;
                const double delay = dmin + W * (direction ? 1.0 - phase : phase);
                const double position = (double)i - delay + shift;
                const double whole = floor(position);
                const long k = (long)whole;
                if (k >= 0 && k + 1 < frames) {
                    const double fraction = position - whole;
                    const double wphase = wscale * phase + woff;      /* window phase (wscale 1, woff 0: the tap's phase) */
                    if (wphase > 0.0 && wphase < 1.0)
                        acc += sin(PI * wphase) * ((1.0 - fraction) * y[k] + fraction * y[k + 1]);
                }
            }
            out[i] = acc;
        }
        p += inc;
        if (p >= 1.0f)
            p -= 1.0f;
    }
}

/* 5. abyss_phasor: the phasor of abyss_reader alone: out[i] is the value in use at absolute sample first + i. */
void abyss_phasor(long frames, long first, long reset, float start, float inc, float *out)
{
    float p = start;
    for (long n = reset; n < first + frames; ++n) {
        if (n >= first)
            out[n - first] = p;
        p += inc;
        if (p >= 1.0f)
            p -= 1.0f;
    }
}

/* ---- additions of analysis3/tempo (9 October 2026) ---- */

/* 6. abyss_walk: accumulator states after count0, count0 + stride, ... steps (entries rows of 32, index 2 * line + group). */
void abyss_walk(long count0, long stride, long entries, const float *phase, float increment, float *out)
{
    for (int g = 0; g < GROUPS; ++g)
        for (int k = 0; k < LINES; ++k) {
            float t = phase[2 * k + g];
            for (long m = 0; m < count0; ++m) {
                if (t >= TWO_PI)
                    t -= TWO_PI;
                t += increment;
            }
            for (long e = 0; e < entries; ++e) {
                out[e * 32 + 2 * k + g] = t;
                for (long m = 0; m < stride; ++m) {
                    if (t >= TWO_PI)
                        t -= TWO_PI;
                    t += increment;
                }
            }
        }
}

/* 7. abyss_network_state: abyss_network started from given accumulator states (index 2 * line + group). */
void abyss_network_state(long frames, const float *theta0, const double *input, const int *length, float depth,
                         float increment, const double *attenuation, const double *own,
                         const double *cross, const double *matrix, const double *kernel, int sections,
                         const double *mix, int rows, double *output)
{
    double *buffer = calloc((size_t)GROUPS * LINES * BUFFER, sizeof(double));
    double state[GROUPS][LINES][MAX_SECTIONS][2] = {{{{0.0}}}};
    float theta[GROUPS][LINES];
    if (sections > MAX_SECTIONS)
        sections = MAX_SECTIONS;
    for (int g = 0; g < GROUPS; ++g)
        for (int k = 0; k < LINES; ++k)
            theta[g][k] = theta0[2 * k + g];
    for (long m = 0; m < frames; ++m) {
        for (int g = 0; g < GROUPS; ++g) {
            double a[LINES];
            for (int k = 0; k < LINES; ++k) {
                const float t = step_accumulator(&theta[g][k], increment);
                const float modulation = depth * oscillator_sine(t);
                const float single = (float)length[g * LINES + k] + modulation;
                const double line_length = (double)single;
                const long whole = (long)floor(line_length);
                const double fraction = line_length - (double)whole;
                const double *line = buffer + (size_t)(g * LINES + k) * BUFFER;
                a[k] = attenuation[g * LINES + k]
                       * ((1.0 - fraction) * line[(m - whole) & MASK] + fraction * line[(m - whole - 1) & MASK]);
            }
            for (int r = 0; r < rows; ++r) {
                double sum = 0.0;
                for (int k = 0; k < LINES; ++k)
                    sum += mix[r * LINES + k] * a[k];
                output[((size_t)m * rows + r) * GROUPS + g] = sum;
            }
            for (int j = 0; j < LINES; ++j) {
                double x = 0.0;
                for (int k = 0; k < LINES; ++k)
                    x += matrix[j * LINES + k] * a[k];
                for (int section = 0; section < sections; ++section) {
                    const double *c = kernel + 5 * section;
                    double *s = state[g][j][section];
                    const double y = c[0] * x + s[0];
                    s[0] = c[1] * x - c[3] * y + s[1];
                    s[1] = c[2] * x - c[4] * y;
                    x = y;
                }
                buffer[(size_t)(g * LINES + j) * BUFFER + (m & MASK)] =
                    own[j] * input[2 * m + g] + cross[j] * input[2 * m + 1 - g] + x;
            }
        }
    }
    free(buffer);
}
