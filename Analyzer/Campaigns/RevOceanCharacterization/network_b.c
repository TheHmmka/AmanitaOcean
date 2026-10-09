/* Sample-by-sample core of the Macro 0 network model of packet network_b.
 *
 * Two independent groups (one per output) of 16 modulated delay lines, each
 * group closed through a 16 x 16 feedback matrix and one loop kernel.
 * network_b.py compiles this file on first use, supplies every constant and
 * documents the elements; findings/network_b.md has the evidence.
 *
 * Per internal (44.1 kHz) sample m and group:
 *   theta_k   single-precision phase accumulator of line k: wrap test, use, add
 *   L_k       = length_k + depth * sine(theta_k)             length of the sample being read, in single
 *               precision: sine, product and sum are each rounded (see lfo_sine)
 *   r_k       = (1 - f) w_k[m - i] + f w_k[m - i - 1]         i = floor(L_k), f = L_k - i
 *   a_k       = attenuation_k * r_k                           tap signal and feedback signal
 *   out       = sum_k tap_k * a_k
 *   w_j[m]    = own_j * in_own[m] + cross_j * in_other[m] + kernel(sum_k matrix[j][k] * a_k)
 * The loop kernel is one filter for all lines: SECTIONS second-order sections in series, each given as
 * b0, b1, b2, a1, a2 (a0 = 1) and run in transposed direct form II.
 */
#include <math.h>
#include <stdlib.h>

#define LINES 16
#define GROUPS 2
#define BUFFER 32768          /* above the longest line at Size 200 % (15178 + 39 + 2 samples) */
#define MASK (BUFFER - 1)
#define SECTIONS 2

static const float TWO_PI = 6.283185307179586f;
static const double HALF_PI = 1.5707963267948966;
static const double HALF_PI_SINGLE = (double)1.5707963267948966f;

/* The reference's sine as far as it was measured: a single-precision result whose argument is reduced by
 * the nearest multiple of the single-precision value of pi/2, so that its period is fl32(2 pi).
 * Compile without contraction of floating-point expressions: the product depth * sine and the sum with
 * the whole length are rounded separately. */
static float lfo_sine(float theta) {
    double quadrant = nearbyint((double)theta / HALF_PI);
    return (float)sin((double)theta - quadrant * (HALF_PI_SINGLE - HALF_PI));
}

/* frames:      internal samples to render; sample 0 is absolute internal sample `first`
 * input:       [frames][2] signal at the line inputs (behind the input equaliser)
 * length:      [2][16] whole line lengths
 * phase:       [32] accumulator values at absolute sample 0, index 2 * line + group
 * attenuation, own, cross, tap: [2][16]
 * matrix:      [2][16][16], row = line written, column = line read
 * kernel:      [SECTIONS][5] coefficients b0, b1, b2, a1, a2
 * output:      [frames][2] sum of the taps of each group
 * reads:       [frames][32] attenuated read of every line (index 16 * group + line), or NULL */
void network_core(long frames, long first, const double *input, const int *length, float depth,
                  const float *phase, float increment, const double *attenuation, const double *own,
                  const double *cross, const double *tap, const double *matrix, const double *kernel,
                  double *output, double *reads) {
    double *buffer = calloc((size_t)GROUPS * LINES * BUFFER, sizeof(double));
    double state[GROUPS][LINES][SECTIONS][2] = {{{{0.0}}}};
    float theta[GROUPS][LINES];
    for (int g = 0; g < GROUPS; ++g)
        for (int k = 0; k < LINES; ++k) {
            float t = phase[2 * k + g];
            for (long m = 0; m < first; ++m) {
                if (t >= TWO_PI) t -= TWO_PI;
                t += increment;
            }
            theta[g][k] = t;
        }
    for (long m = 0; m < frames; ++m) {
        for (int g = 0; g < GROUPS; ++g) {
            double a[LINES], sum = 0.0;
            for (int k = 0; k < LINES; ++k) {
                float t = theta[g][k];
                if (t >= TWO_PI) t -= TWO_PI;
                theta[g][k] = t + increment;
                float modulation = depth * lfo_sine(t);
                double line_length = (double)((float)length[g * LINES + k] + modulation);
                long whole = (long)floor(line_length);
                double fraction = line_length - (double)whole;
                const double *line = buffer + (size_t)(g * LINES + k) * BUFFER;
                a[k] = attenuation[g * LINES + k]
                       * ((1.0 - fraction) * line[(m - whole) & MASK] + fraction * line[(m - whole - 1) & MASK]);
                sum += tap[g * LINES + k] * a[k];
                if (reads) reads[(size_t)m * GROUPS * LINES + g * LINES + k] = a[k];
            }
            output[2 * m + g] = sum;
            const double *rows = matrix + (size_t)g * LINES * LINES;
            for (int j = 0; j < LINES; ++j) {
                double x = 0.0;
                for (int k = 0; k < LINES; ++k) x += rows[j * LINES + k] * a[k];
                for (int section = 0; section < SECTIONS; ++section) {
                    const double *c = kernel + 5 * section;
                    double *s = state[g][j][section];
                    double y = c[0] * x + s[0];
                    s[0] = c[1] * x - c[3] * y + s[1];
                    s[1] = c[2] * x - c[4] * y;
                    x = y;
                }
                buffer[(size_t)(g * LINES + j) * BUFFER + (m & MASK)] =
                    own[g * LINES + j] * input[2 * m + g] + cross[g * LINES + j] * input[2 * m + 1 - g] + x;
            }
        }
    }
    free(buffer);
}
