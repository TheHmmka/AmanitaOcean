/* Sample-by-sample core of the campaign's network model of Rev OCEAN (Tide mode, Macro 0).
 *
 * network_model.py compiles this file on first use, supplies every constant and documents the
 * elements; findings/network.md has the evidence.
 *
 *     cc -O2 -shared -fPIC -ffp-contract=off -o network_model.dylib network_model.c
 *
 * Everything runs at the internal rate of 44.1 kHz. Two independent groups (one per output channel)
 * of 16 modulated delay lines, each group closed through the same 16 x 16 matrix and one loop kernel.
 * Signals are double precision; the oscillators and the line lengths are single precision, as measured.
 *
 * Per internal sample m and group g, in this order:
 *   1. theta_k   phase accumulator of line k: if (theta >= 2 pi) theta -= 2 pi; use; theta += increment
 *   2. L_k       = fl32(fl32(N_k) + fl32(depth * sine(theta_k)))    the three roundings are separate
 *   3. r_k       = (1 - f) w_k[m - i] + f w_k[m - i - 1]            i = floor(L_k), f = L_k - i
 *   4. a_k       = attenuation_k * r_k                              output tap and feedback signal of line k
 *   5. out_r     = sum_k mix[r][k] a_k                              one output per row of `mix` (the output
 *                                                                   taps of a group of lines, or of one line)
 *   6. v_j       = sum_k matrix[j][k] a_k
 *   7. w_j[m]    = own_j x_g[m] + cross_j x_other[m] + (kernel * v_j)[m]
 * The loop kernel is `sections` second-order sections in series, each b0, b1, b2, a1, a2 (a0 = 1), run
 * in transposed direct form II with one state per line written.
 *
 * `oscillator_phases` returns step 1 on its own, and `network_core` accepts line lengths computed
 * elsewhere in place of steps 1 and 2; fit_network.py uses both to test other expressions of the length.
 */
#include <math.h>
#include <stdlib.h>

#define LINES 16
#define GROUPS 2
#define BUFFER 32768          /* power of two above the longest line at Size 200 % (15178 + 39 + 2 samples) */
#define MASK (BUFFER - 1)
#define MAX_SECTIONS 4

static const float TWO_PI = 6.283185307179586f;
static const double HALF_PI = 1.5707963267948966;
static const double HALF_PI_SINGLE = (double)1.5707963267948966f;

/* The reference's sine as far as it is measured: a single-precision result whose argument is reduced by
 * the nearest multiple of the single-precision value of pi/2. Each quadrant therefore lags the true
 * sine by another 4.37e-8 rad. The last place of the reference's sine is not identified. */
static float oscillator_sine(float theta)
{
    const double quadrant = nearbyint((double)theta / HALF_PI);
    return (float)sin((double)theta - quadrant * (HALF_PI_SINGLE - HALF_PI));
}

/* The accumulators at absolute internal sample `first`: every earlier sample has done its wrap test
 * and its addition. phase: [32] start values, index 2 * line + group. */
static void start_accumulators(long first, const float *phase, float increment, float theta[GROUPS][LINES])
{
    for (int g = 0; g < GROUPS; ++g)
        for (int k = 0; k < LINES; ++k) {
            float t = phase[2 * k + g];
            for (long m = 0; m < first; ++m) {
                if (t >= TWO_PI)
                    t -= TWO_PI;
                t += increment;
            }
            theta[g][k] = t;
        }
}

/* The phase a line uses at this sample; the accumulator is advanced for the next one. */
static float step_accumulator(float *theta, float increment)
{
    float t = *theta;
    if (t >= TWO_PI)
        t -= TWO_PI;
    *theta = t + increment;
    return t;
}

/* theta: [frames][2][16], the phase every line uses at absolute internal samples first .. first + frames - 1 */
void oscillator_phases(long frames, long first, const float *phase, float increment, float *theta)
{
    float state[GROUPS][LINES];
    start_accumulators(first, phase, increment, state);
    for (long m = 0; m < frames; ++m)
        for (int g = 0; g < GROUPS; ++g)
            for (int k = 0; k < LINES; ++k)
                theta[((size_t)m * GROUPS + g) * LINES + k] = step_accumulator(&state[g][k], increment);
}

/* frames:      internal samples to render; sample 0 is absolute internal sample `first`, counted from
 *              the first sample the plug-in processes (the oscillators start there)
 * input:       [frames][2] signal at the line inputs (behind the input equaliser)
 * length:      [2][16] whole line lengths N
 * depth:       modulation depth in samples (single precision)
 * phase:       [32] accumulator values at absolute sample 0, index 2 * line + group
 * attenuation: [2][16] gain of one pass through a line
 * own, cross:  [16] input gains for the group's own and the other input channel
 * matrix:      [16][16] row = line written, column = line read
 * kernel:      [sections][5]
 * mix:         [rows][16] weights of the outputs
 * lengths:     [frames][2][16] single-precision line lengths to use in place of steps 1 and 2, or NULL
 * output:      [frames][rows][2] */
void network_core(long frames, long first, const double *input, const int *length, float depth,
                  const float *phase, float increment, const double *attenuation, const double *own,
                  const double *cross, const double *matrix, const double *kernel, int sections,
                  const double *mix, int rows, const float *lengths, double *output)
{
    double *buffer = calloc((size_t)GROUPS * LINES * BUFFER, sizeof(double));
    double state[GROUPS][LINES][MAX_SECTIONS][2] = {{{{0.0}}}};
    float theta[GROUPS][LINES];
    if (sections > MAX_SECTIONS)
        sections = MAX_SECTIONS;
    start_accumulators(first, phase, increment, theta);
    for (long m = 0; m < frames; ++m) {
        for (int g = 0; g < GROUPS; ++g) {
            double a[LINES];
            for (int k = 0; k < LINES; ++k) {
                float single;
                if (lengths) {
                    single = lengths[((size_t)m * GROUPS + g) * LINES + k];
                } else {
                    const float modulation = depth * oscillator_sine(step_accumulator(&theta[g][k], increment));
                    single = (float)length[g * LINES + k] + modulation;
                }
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
