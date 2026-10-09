/* Sample-by-sample core of the network_a model of Rev OCEAN (Tide mode, Macro 0).
 *
 * network_a.py compiles this file on first use and calls it through ctypes:
 *
 *     cc -O2 -shared -fPIC -ffp-contract=off -o network_a.dylib network_a.c
 *
 * Everything here runs at the internal rate of 44.1 kHz. One call of
 * `network_group` renders one group of sixteen delay lines (one output
 * channel). Signals are double precision; the oscillators and the line
 * lengths are single precision, as measured on the reference.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LINES 16
#define BUFFER 16384            /* power of two above the longest line at Size 200 % plus its modulation */
#define MASK (BUFFER - 1)
#define MAX_ORDER 64            /* longest loop filter (taps of b or a), a power of two */

/* Sine of an oscillator phase in [0, 2 pi): the phase is folded into [-pi/2, pi/2] with the single
 * precision values of pi and 2 pi (both subtractions are exact), so the result differs from
 * sin(theta) by 0.9e-7 rad of phase in the middle half of the cycle and 1.7e-7 rad in the last quarter. */
static float folded_sine(float theta)
{
    const float pi = (float)M_PI, two_pi = (float)(2.0 * M_PI);
    if (theta < (float)(0.5 * M_PI))
        return (float)sin((double)theta);
    if (theta < (float)(1.5 * M_PI))
        return (float)sin((double)(float)(pi - theta));
    return (float)sin((double)(float)(theta - two_pi));
}

/* Line lengths of one group while internal samples start .. start + frames - 1 are read.
 *
 * Accumulator k starts at fl32(phase_step * k) when processing starts and runs
 *     if (theta >= 2 pi) theta -= 2 pi;   use theta;   theta += increment
 * in single precision once per internal sample. The length is
 *     whole + (depth_seconds * sine) * rate
 * with both products and the sum rounded to single precision. */
void line_lengths(long start, long frames, const int *accumulator, const double *whole, double depth_seconds,
                  double rate, double phase_step, double increment, float *lengths)
{
    const float two_pi = (float)(2.0 * M_PI);
    const float step = (float)increment;
    const float depth = (float)depth_seconds, rate_f = (float)rate;
    for (int line = 0; line < LINES; ++line) {
        float theta = (float)phase_step * (float)accumulator[line];
        const float whole_f = (float)whole[line];
        for (long index = 0; index < start + frames; ++index) {
            if (theta >= two_pi)
                theta -= two_pi;
            if (index >= start) {
                volatile float seconds = depth * folded_sine(theta);
                volatile float samples = seconds * rate_f;
                lengths[(index - start) * LINES + line] = whole_f + samples;
            }
            theta += step;
        }
    }
}

/* One group of sixteen lines.
 *
 * Per internal sample t:
 *     read_k   = linear interpolation of line k at lengths[t][k]
 *     tap_k    = attenuation[k] * read_k
 *     fed_k    = (loop * tap_k)[t]                       loop = loop_b(z) / loop_a(z), loop_a[0] = 1
 *     excitation[t] = sum_k output_weight[k] * (output_after_loop ? fed_k : tap_k)
 *     write_j  = own_gain[j] * own[t] + cross_gain[j] * cross[t] + inject[t][j] + sum_k matrix[j][k] * fed_k
 *
 * `own`, `cross`, `inject` (frames x 16) and `taps` (frames x 16, receives what the output sums) may be NULL.
 */
void network_group(long frames, const float *lengths, const double *own, const double *cross,
                   const double *own_gain, const double *cross_gain, const double *attenuation,
                   const double *output_weight, const double *matrix,
                   const double *loop_b, int loop_b_count, const double *loop_a, int loop_a_count,
                   int output_after_loop, const double *inject, double *excitation, double *taps)
{
    double *buffer = calloc((size_t)LINES * BUFFER, sizeof(double));
    double (*loop_in)[MAX_ORDER] = calloc(LINES, sizeof *loop_in);
    double (*loop_out)[MAX_ORDER] = calloc(LINES, sizeof *loop_out);
    double tap[LINES], fed[LINES];
    int feedback = 0;
    for (int index = 0; index < LINES * LINES; ++index)
        feedback |= matrix[index] != 0.0;
    const int filtered = feedback || output_after_loop;

    for (long t = 0; t < frames; ++t) {
        const float *length = lengths + t * LINES;
        const int slot = (int)(t & (MAX_ORDER - 1));
        double sum = 0.0;
        for (int line = 0; line < LINES; ++line) {
            const double position = (double)length[line];
            const long whole = (long)position;
            const double fraction = position - (double)whole;
            const double *data = buffer + (size_t)line * BUFFER;
            const double read = (1.0 - fraction) * data[(t - whole) & MASK] + fraction * data[(t - whole - 1) & MASK];
            tap[line] = attenuation[line] * read;
            fed[line] = tap[line];
            if (filtered) {
                double value = 0.0;
                loop_in[line][slot] = tap[line];
                for (int n = 0; n < loop_b_count; ++n)
                    value += loop_b[n] * loop_in[line][(slot - n) & (MAX_ORDER - 1)];
                for (int n = 1; n < loop_a_count; ++n)
                    value -= loop_a[n] * loop_out[line][(slot - n) & (MAX_ORDER - 1)];
                loop_out[line][slot] = value;
                fed[line] = value;
            }
            sum += output_weight[line] * (output_after_loop ? fed[line] : tap[line]);
        }
        excitation[t] = sum;
        if (taps)
            memcpy(taps + t * LINES, output_after_loop ? fed : tap, sizeof tap);
        for (int line = 0; line < LINES; ++line) {
            double value = 0.0;
            if (own)
                value += own_gain[line] * own[t];
            if (cross)
                value += cross_gain[line] * cross[t];
            if (inject)
                value += inject[t * LINES + line];
            if (feedback) {
                const double *row = matrix + line * LINES;
                for (int from = 0; from < LINES; ++from)
                    value += row[from] * fed[from];
            }
            buffer[(size_t)line * BUFFER + (t & MASK)] = value;
        }
    }
    free(buffer);
    free(loop_in);
    free(loop_out);
}

/* Converter kernel at a distance of numerator / 160 internal samples (output time minus input time):
 * the table entry at or below the distance, and the entry below that when the distance falls exactly
 * on an entry of the wing `low_wing` (+1: inputs before the output sample, -1: inputs after it). */
static double kernel(const double *table, long table_length, long entries, long numerator, int low_wing)
{
    const long scaled = (numerator < 0 ? -numerator : numerator) * entries;
    long index = scaled / 160;
    if (scaled % 160 == 0 && ((numerator > 0 && low_wing > 0) || (numerator < 0 && low_wing < 0)))
        index -= 1;
    if (index < 0)
        index = 0;
    return index < table_length ? table[index] : 0.0;
}

static long floor_divide(long value, long divisor)
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

/* Input converter of a 48 kHz host. Host sample n (counted from the first processed sample) is centred
 * at lattice position 147 n + offset, internal sample m sits at 160 m (lattice of 1/160 internal sample).
 * `host` holds host samples host_start .. host_start + host_frames - 1; the result is internal samples
 * first .. first + frames - 1, scaled by 147/160. */
void convert_in(const double *host, long host_frames, long host_start, long offset,
                const double *table, long table_length, long entries, int reach, int low_wing,
                long first, long frames, double *internal)
{
    const long span = (160 * (long)reach) / 147 + 2;
    for (long m = 0; m < frames; ++m) {
        const long position = 160 * (first + m) - offset;
        const long centre = floor_divide(position, 147);
        double sum = 0.0;
        for (long n = centre - span; n <= centre + span; ++n) {
            const long local = n - host_start;
            if (local < 0 || local >= host_frames || host[local] == 0.0)
                continue;
            sum += host[local] * kernel(table, table_length, entries, position - 147 * n, low_wing);
        }
        internal[m] = sum * 147.0 / 160.0;
    }
}

/* Output converter of a 48 kHz host: host sample N reads the internal stream at lattice position
 * 147 N - offset. `internal` holds internal samples first .. first + frames - 1. */
void convert_out(const double *internal, long first, long frames, long offset,
                 const double *table, long table_length, long entries, int reach, int low_wing,
                 long host_start, long host_frames, double *host)
{
    for (long index = 0; index < host_frames; ++index) {
        const long position = 147 * (host_start + index) - offset;
        const long centre = floor_divide(position, 160);
        double sum = 0.0;
        for (long m = centre - reach - 1; m <= centre + reach + 1; ++m) {
            const long local = m - first;
            if (local < 0 || local >= frames)
                continue;
            sum += internal[local] * kernel(table, table_length, entries, position - 160 * m, low_wing);
        }
        host[index] = sum;
    }
}
