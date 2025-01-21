/* some tests with the Si5351 using the continued fraction algorithm
 *
 * Copyright 2024, Franco Venturi, K4VZ
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_CLOCKS 3

static const double SI5351_MIN_VCO_FREQ = 600e6;
static const double SI5351_MAX_VCO_FREQ = 1000e6;
static const uint32_t SI5351_MAX_DENOMINATOR = 1048575;
static const double SI5351_MIN_CLKIN_FREQ = 10e6;
static const double SI5351_MAX_CLKIN_FREQ = 100e6;

static const double CLOCK_TOLERANCE = 1e-8;
 

static void rational_approximation(double value, uint32_t max_denominator,
                                   uint32_t *a, uint32_t *b, uint32_t *c);

int main(int argc, char **argv) {
    double xtal;
    double clks[MAX_CLOCKS];

    if (argc > MAX_CLOCKS + 2) {
        fprintf(stderr, "Too many arguments - maximum number of clocks is: %d\n", MAX_CLOCKS);
        return EXIT_FAILURE;
    }
    sscanf(argv[1], "%lf", &xtal);
    for (int i = 2; i < argc; i++) {
        sscanf(argv[i], "%lf", &clks[i-2]);
    }
    int nclks = argc - 2;

    /* make sure xtal (CLKIN) is in the allowed 10-100MHz range */
    if (xtal < SI5351_MIN_CLKIN_FREQ || xtal > SI5351_MAX_CLKIN_FREQ) {
        fprintf(stderr, "XTAL reference (CKLIN) is out of range");
        return EXIT_FAILURE;
    }

    /* bring xtal (CLKIN) within the 10-40MHz range using CLKIN_DIV */
    double xtal_orig = xtal;
    uint8_t clkin_div = 0;
    while (xtal > 40e6 && clkin_div <= 3) {
        xtal /= 2.0;
        clkin_div += 1;
    }
    if (clkin_div > 0) {
        fprintf(stdout, "--> CLKIN_DIV=%d\n", clkin_div);
        fprintf(stdout, "\n");
    }
    int xtal_div = 1 << clkin_div;

    /* if the requested clock is below 1MHz, use an R divider */
    double r_clk0 = clks[0];
    uint8_t rdiv = 0;
    while (r_clk0 < 1e6 && rdiv <= 7) {
        r_clk0 *= 2.0;
        rdiv += 1;
    }
    if (r_clk0 < 1e6) {
        fprintf(stderr, "requested clock is too low: %'.0lf\n", clks[0]);
        return EXIT_FAILURE;
    }

    /* first scenario - N-frac for feedback MS and even integer for output MS */
    fprintf(stdout, "first scenario - N-frac for feedback MS and even integer for output MS\n");
    fprintf(stdout, "\n");

    /* choose an even integer for the output MS */
    uint32_t output_ms = ((uint32_t)(SI5351_MAX_VCO_FREQ / r_clk0));
    output_ms -= output_ms % 2;
    if (output_ms < 4 || output_ms > 900) {
        fprintf(stderr, "invalid output MS: %d (clock=%'.0lf)\n", output_ms, clks[0]);
        return EXIT_FAILURE;
    }

    /* try different values for f_VCO */
    while (1) {
        double f_vco = r_clk0 * output_ms;
        if (output_ms < 4 || f_vco < SI5351_MIN_VCO_FREQ)
            break;

        /* feedback MS */
        double feedback_ms = f_vco / xtal;

        if (feedback_ms < 15 || feedback_ms > 90) {
            fprintf(stderr, "invalid feedback MS: %'.0lf (xtal=%'.0lf/%d, output MS=%d, f_VCO=%'.0lf)\n", feedback_ms, xtal_orig, xtal_div, output_ms, f_vco);
            fprintf(stderr, "\n");
            output_ms -= 2;
            continue;
        }

        /* find a good rational approximation for feedback_ms */
        uint32_t a;
        uint32_t b;
        uint32_t c;
        rational_approximation(feedback_ms, SI5351_MAX_DENOMINATOR, &a, &b, &c);

        char *is_integer = "";
        if (b == 0) {
            is_integer = a % 2 ? "   -> integer" : "   -> even integer";
        } 
        double actual_ratio = a + (double)b / (double)c;
        double actual_pll_freq = xtal * actual_ratio;
        fprintf(stdout, "actual PLL frequency: %'.0lf/%d * (%d + %d / %d) = %'.0lf%s\n", xtal_orig, xtal_div, a, b, c, actual_pll_freq, is_integer);

        double actual_clk0 = actual_pll_freq / output_ms / (1 << rdiv);
        fprintf(stdout, "actual clock 0: %'.0lf / %d = %'.0lf\n", actual_pll_freq, output_ms * (1 << rdiv), actual_clk0);
        double clk_diff = actual_clk0 - clks[0];
        if (clk_diff <= -CLOCK_TOLERANCE || clk_diff >= CLOCK_TOLERANCE) {
            fprintf(stdout, "*** clock 0 difference: %'.0lg\n", clk_diff);
        }

        /* additional clocks */
        for (int nclk = 1; nclk < nclks; nclk++) {
            double clk_output_ms = actual_pll_freq / clks[nclk];
            /* find a good rational approximation for feedback_ms */
            uint32_t a;
            uint32_t b;
            uint32_t c;
            rational_approximation(clk_output_ms, SI5351_MAX_DENOMINATOR, &a, &b, &c);

            double clk_actual_ratio = a + (double)b / (double)c;
            if (clk_actual_ratio < 4 || clk_actual_ratio > 900) {
                continue;
            }

            char *is_integer = "";
            if (b == 0) {
                is_integer = a % 2 ? "   -> integer" : "   -> even integer";
            }
            double actual_clk = actual_pll_freq / clk_actual_ratio;
            fprintf(stdout, "actual clock %d: %'.0lf / (%d + %d / %d) = %'.0lf%s\n", nclk, actual_pll_freq, a, b, c, actual_clk, is_integer);
            double clk_diff = actual_clk - clks[nclk];
            if (clk_diff <= -CLOCK_TOLERANCE || clk_diff >= CLOCK_TOLERANCE) {
                fprintf(stdout, "*** clock %d difference: %'.0lg\n", nclk, clk_diff);
            }
        }

        fprintf(stdout, "\n");
        output_ms -= 2;
    }

    fprintf(stdout, "\n");

    /* second scenario - even integer for feedback MS and N-frac for output MS */
    fprintf(stdout, "second scenario - even integer for feedback MS and N-frac for output MS\n");
    fprintf(stdout, "\n");

    /* choose an even integer for the feedback MS */
    uint32_t feedback_ms = ((uint32_t)(SI5351_MAX_VCO_FREQ / xtal));
    feedback_ms -= feedback_ms % 2;
    if (feedback_ms < 16) {
        fprintf(stderr, "invalid feedback MS: %d (xtal=%'.0lf/%d, output MS=%d, f_VCO=%'.0lf)\n", feedback_ms, xtal_orig, xtal_div, clkin_div, output_ms, feedback_ms * xtal);
        return EXIT_FAILURE;
    }
    if (feedback_ms > 90) {
        feedback_ms = 90;
        if (xtal * feedback_ms < SI5351_MIN_VCO_FREQ) {
            fprintf(stderr, "invalid feedback MS: %d (xtal=%'.0lf/%d)\n", feedback_ms, xtal_orig, xtal_div);
            return EXIT_FAILURE;
        }
    }

    /* try different values for f_VCO */
    while (1) {
        double f_vco = xtal * feedback_ms;
        if (feedback_ms < 16 || f_vco < SI5351_MIN_VCO_FREQ)
            break;

        /* output MS */
        double output_ms = f_vco / r_clk0;
        /* find a good rational approximation for output_ms */
        uint32_t a;
        uint32_t b;
        uint32_t c;
        rational_approximation(output_ms, SI5351_MAX_DENOMINATOR, &a, &b, &c);

        fprintf(stdout, "actual PLL frequency: %'.0lf/%d * %d\n", xtal_orig, xtal_div, feedback_ms);

        double actual_pll_freq = xtal * feedback_ms;
        fprintf(stdout, "actual PLL frequency: %'.0lf\n", actual_pll_freq);

        char *is_integer = "";
        if (b == 0) {
            is_integer = a % 2 ? "   -> integer" : "   -> even integer";
        } 
        double actual_ratio = a + (double)b / (double)c;
        double actual_clk0 = actual_pll_freq / actual_ratio / (1 << rdiv);
        fprintf(stdout, "actual clock 0: %'.0lf / (%d + %d / %d) / %d = %'.0lf%s\n", actual_pll_freq, a, b, c, 1 << rdiv, actual_clk0, is_integer);
        double clk_diff = actual_clk0 - clks[0];
        if (clk_diff <= -CLOCK_TOLERANCE || clk_diff >= CLOCK_TOLERANCE) {
            fprintf(stdout, "*** clock 0 difference: %'.0lg\n", clk_diff);
        }

        /* additional clocks */
        for (int nclk = 1; nclk < nclks; nclk++) {
            double clk_output_ms = actual_pll_freq / clks[nclk];
            /* find a good rational approximation for feedback_ms */
            uint32_t a;
            uint32_t b;
            uint32_t c;
            rational_approximation(clk_output_ms, SI5351_MAX_DENOMINATOR, &a, &b, &c);

            double clk_actual_ratio = a + (double)b / (double)c;
            if (clk_actual_ratio < 4 || clk_actual_ratio > 900) {
                continue;
            }

            char *is_integer = "";
            if (b == 0) {
                is_integer = a % 2 ? "   -> integer" : "   -> even integer";
            }
            double actual_clk = actual_pll_freq / clk_actual_ratio;
            fprintf(stdout, "actual clock %d: %'.0lf / (%d + %d / %d) = %'.0lf%s\n", nclk, actual_pll_freq, a, b, c, actual_clk, is_integer);
            double clk_diff = actual_clk - clks[nclk];
            if (clk_diff <= -CLOCK_TOLERANCE || clk_diff >= CLOCK_TOLERANCE) {
                fprintf(stdout, "*** clock %d difference: %'.0lg\n", nclk, clk_diff);
            }
        }

        fprintf(stdout, "\n");
        feedback_ms -= 2;
    }

    return EXIT_SUCCESS;
}


/* best rational approximation:
 *
 *     value ~= a + b/c     (where c <= max_denominator)
 *
 * References:
 * - https://en.wikipedia.org/wiki/Continued_fraction#Best_rational_approximations
 */
static void rational_approximation(double value, uint32_t max_denominator,
                                   uint32_t *a, uint32_t *b, uint32_t *c)
{
    const double epsilon = 1e-5;

    double af;
    double f0 = modf(value, &af);
    *a = (uint32_t) af;
    *b = 0;
    *c = 1;
    double f = f0;
    double delta = f0;
    /* we need to take into account that the fractional part has a_0 = 0 */
    uint32_t h[] = {1, 0};
    uint32_t k[] = {0, 1};
    for(int i = 0; i < 100; ++i){
        if(f <= epsilon){
            break;
        }
        double anf;
        f = modf(1.0 / f,&anf);
        uint32_t an = (uint32_t) anf;
        for(uint32_t m = (an + 1) / 2; m <= an; ++m){
            uint32_t hm = m * h[1] + h[0];
            uint32_t km = m * k[1] + k[0];
            if(km > max_denominator){
                break;
            }
            double d = fabs((double) hm / (double) km - f0);
            if(d < delta){
                delta = d;
                *b = hm;
                *c = km;
            }
        }
        uint32_t hn = an * h[1] + h[0];
        uint32_t kn = an * k[1] + k[0];
        h[0] = h[1]; h[1] = hn;
        k[0] = k[1]; k[1] = kn;
    }
    return;
}
